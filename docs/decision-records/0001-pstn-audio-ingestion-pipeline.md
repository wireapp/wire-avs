# Architectural Decision Record (ADR): 16-bit PCM Audio Ingestion Pipeline

## Status
Proposed

## Context
Our application needs to ingest real-time audio packets from a WebRTC stream and move them into an internal processing buffer. The processing buffer handles tasks like jitter management, mixing, and audio analytics. We need a pipeline that ensures low latency, prevents audio stuttering, and maintains strict thread safety. 

The entry point is the WebRTC audioCallback function, which triggers on a high-priority, real-time audio thread. The destination is our application's MediaInternalBuffer, which operates on a separate processing thread. The system is implemented in C/C++ and processes raw 16-bit signed integer PCM samples.

## Decision
We will implement an asynchronous ingestion pipeline using a lock-free single-producer single-consumer (SPSC) ring buffer for the audio packets. 

    [WebRTC Audio Thread] 
           │
           ▼ (audioCallback passes int16_t*)
    [PCM Audio Extraction & Validation]
           │
           ▼ (Lock-Free Push via std::atomic)
    [Lock-Free SPSC Ring Buffer (int16_t)] 
           │
           ▼ (Pop via Worker Thread)
    [Worker Processing Thread]
           │
           ▼
    [MediaInternalBuffer]

### 1. Ingestion Flow
* Audio Thread Callback: WebRTC receives the raw audio frame and invokes audioCallback(const int16_t* audio_data, size_t num_samples).
* Extraction & Validation: The 16-bit PCM data is validated for sample count and valid pointers directly on the real-time audio thread.
* Enqueue (Push): The raw int16_t samples are copied into the SPSC ring buffer. If the buffer has insufficient space, the remaining samples are dropped, and an audio glitch counter is incremented.
* Worker Thread Dispatch: A dedicated worker thread polls the ring buffer or is woken up by a lightweight signaling mechanism.
* Dequeue (Pop): The worker thread extracts the int16_t data from the ring buffer.
* Internal Buffering: The worker thread wraps the samples into our internal audio frame structure and inserts it into the ordered MediaInternalBuffer.

### 2. Threading Model
* Producer: Only the WebRTC real-time audio thread writes to the ring buffer.
* Consumer: Only the dedicated worker thread reads from the ring buffer.
* Isolation: No mutexes, heap allocations (malloc/new), or blocking I/O are permitted inside audioCallback to prevent priority inversion. Thread safety is enforced strictly via std::atomic memory barriers with std::memory_order_release and std::memory_order_acquire.

### 3. Memory Management
* Pre-allocation: The ring buffer memory is fully allocated at startup with a contiguous block of int16_t elements.
* Contiguous Alignment: Memory layout matches int16_t to allow high-throughput vector copy operations (std::memcpy) when wrapping around the ring buffer array.

## Consequences

### Pros
* Ultra-Low Latency: Lock-free operations eliminate thread contention and audio glitching on the high-priority audio thread.
* No Runtime Allocation: Pre-allocated int16_t arrays prevent non-deterministic page faults or heap delays during audio processing.
* Clear Ownership: The SPSC pattern makes data ownership explicit and simplifies debugging of memory leaks or race conditions.

### Cons
* Fixed Bounds: A fixed-size ring buffer will drop audio data during uncharacteristic CPU spikes if the worker thread falls behind.
* Memory Footprint: Pre-allocating maximum burst sizes increases the baseline memory usage per audio stream.
