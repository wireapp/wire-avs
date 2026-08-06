# Architectural Decision Record (ADR): Bidirectional 16-bit PCM Audio Pipeline

## Status
Accepted

## Context
Our application needs to handle bidirectional, real-time audio streams via WebRTC. It must ingest incoming audio packets into our application for internal processing (jitter management, mixing, and analytics) and concurrently deliver outgoing mixed audio back to WebRTC for hardware playback. Both paths require ultra-low latency, strict thread safety, and must prevent audio stuttering or glitches.

* **Ingestion (Capture) Path:** The entry point is the WebRTC capture callback, which triggers on a high-priority, real-time hardware recording thread. The destination is the application's `MediaInternalBuffer` on a separate worker thread.
* **Playout (Render) Path:** The exit point is the native WebRTC `AudioTransport::NeedMorePlayData` callback. This triggers on a high-priority, real-time hardware playback thread. The source is the application's processing thread via `MediaInternalBuffer`.

The entire system is implemented in C/C++ and processes raw 16-bit signed integer PCM samples.

## Decision
We will implement an asynchronous, bidirectional pipeline utilizing two independent, lock-free single-producer single-consumer (SPSC) ring buffers. This completely isolates the real-time WebRTC threads from the application's processing overhead.

     [INGESTION / CAPTURE PATH]
        [WebRTC Audio Thread] 
               │
               ▼ (audioCallback passes int16_t*)
        [PCM Audio Extraction & Validation]
               │
               ▼ (Lock-Free Push via Ingest SPSC)
        [Ingest SPSC Ring Buffer (int16_t)] 
               │
               ▼ (Pop via Worker Thread)
        [Worker Processing Thread] <──> [MediaInternalBuffer (Mixing / Analytics)]
               │
               ▼ (Push via Worker Thread)
        [Playout SPSC Ring Buffer (int16_t)] 
               │
               ▼ (Pop via WebRTC Thread)
        [webrtc::AudioTransport::NeedMorePlayData]
               │
               ▼ (Populates audioSamples buffer)
        [WebRTC Audio Playout Engine]
     [PLAYOUT / RENDER PATH]

### 1. Ingestion Flow (Capture)
* **Audio Thread Callback**: WebRTC receives the raw captured frame from the microphone and invokes `audioCallback(const int16_t* audio_data, size_t num_samples)`.
* **Extraction & Validation**: The 16-bit PCM data is validated for sample count and valid pointers directly on the real-time audio thread.
* **Enqueue (Push)**: The raw `int16_t` samples are copied into the **Ingest SPSC ring buffer**. If the buffer has insufficient space, incoming samples are dropped, and an ingestion glitch counter is incremented.
* **Worker Processing**: A dedicated internal worker thread extracts the `int16_t` data from the Ingest SPSC ring buffer, wraps the samples into our internal audio frame structure, and inserts them into the ordered `MediaInternalBuffer`.

### 2. Playout Flow (Render)
* **Upstream Generation**: The worker thread pulls mixed frame data from `MediaInternalBuffer` and segments it into the standard 10ms chunks expected by WebRTC.
* **Enqueue (Push)**: The worker thread pushes these raw `int16_t` samples into the **Playout SPSC ring buffer**. If this buffer is full, the thread logs an overflow and applies a backpressure strategy.
* **WebRTC Hardware Request**: The WebRTC Audio Device Module (ADM) engine fires the real-time callback `NeedMorePlayData(size_t nSamples, size_t nBytesPerSample, size_t nChannels, uint32_t samplesPerSec, void* audioSamples, ...)`.
* **Dequeue (Pop)**: Inside `NeedMorePlayData`, the application pops the requested number of `int16_t` samples directly from the Playout SPSC ring buffer and copies them straight into the provided `audioSamples` memory pointer.
* **Underflow Management**: If the ring buffer contains fewer samples than requested, the callback zeroes out the remaining bytes in `audioSamples` to prevent garbage noise playback and increments a playout underrun glitch counter.

### 3. Threading Model
* **Thread Separation**: 
  * **Ingest SPSC Buffer**: Producer is the WebRTC Recording Thread; Consumer is the Application Worker Thread.
  * **Playout SPSC Buffer**: Producer is the Application Worker Thread; Consumer is the WebRTC Playout Thread (`NeedMorePlayData`).
* **Real-time Isolation**: No mutexes, heap allocations (`malloc`/`new`), or blocking I/O operations are permitted inside either the capture callback or `NeedMorePlayData`. Thread safety is strictly enforced via `std::atomic` index counters utilizing `std::memory_order_release` and `std::memory_order_acquire`.

### 4. Memory Management
* **Pre-allocation**: Both the Ingest and Playout ring buffers are fully allocated at system startup as contiguous blocks of `int16_t` elements to eliminate runtime page faults.
* **Contiguous Alignment**: Direct memory copying (`std::memcpy`) is used to read from and write to the ring buffers, handling circular wrap-around boundaries cleanly.

## Consequences

### Pros
* **Ultra-Low Latency**: Lock-free operations eliminate thread contention on both the capture and playout ends of the WebRTC stack.
* **Zero Real-Time Blocking**: The high-priority hardware threads are isolated from the application's business logic, minimizing audio glitching (stuttering).
* **Deterministic Execution**: Eliminating runtime memory allocation inside the real-time paths guarantees execution times stay well within the strict 10ms processing windows.
* **Decoupled Architecture**: Upstream mixing and analytical code can run at separate intervals without being synchronously bound to the hardware soundcard clock.

### Cons
* **Fixed Bounds Vulnerability**: If the internal worker thread experiences CPU starvation and falls behind, the Ingest buffer will overflow (dropping data) and the Playout buffer will underflow (generating silence).
* **Memory Footprint**: Keeping two separate, pre-allocated maximum burst size ring buffers increases the static baseline memory usage per active audio stream.
