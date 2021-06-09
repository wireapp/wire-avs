function vadMask=visqolvad(sig,fs)
% Alternative VAD to simple threshold VAD: 
% This can be replaced with any chosen VAD implementation.
% This implementation is based on G.729 implementation from:
% http://www-mmsp.ece.mcgill.ca/courses/2007-2008/ECSE412B/Project/Project.html

fs_vis=8000;
sig=resample(sig, fs_vis,fs);
fs=8000;

x=sig+1e-10;

CActive = 1;
CSilence = 0;
CSID = 2;

framesize=10e-3; %10ms
Ns = length(x);
NsFrame = framesize*fs;       % Frame size (10ms)
NsLA = NsFrame/2;          % Look-ahead size for VAD / DTX
NsWin = 3*NsFrame;
NFrame = floor(Ns/NsFrame);

% Append zeros to input data (to allow look-ahead for the last frame)
x = [x; zeros(NsFrame, 1)];

% remove dc
HPFilt.b = [1 -1];
HPFilt.a = [1 -127/128];

x_hp = filter(HPFilt.b, HPFilt.a, x); %HP filter

% Set up the quantizer tables
[Yq, Xq, Code, ICode] = QuantMuLawTables;

% Initialize the VAD and DTX
VADPar = InitVADPar;

%x_hp_mem = zeros(NsLA, 1);

for (k = 0:NFrame-1)

  ist = k*NsFrame + 1;
  ifn = ist + NsFrame - 1;       % New data limits
  x_new = x(ist:ifn);
  [Ivd, VADPar] = VAD(x_new, VADPar);
  ivdarray(k+1)=Ivd;
  
end

vadMask=round(resample(ivdarray,NsFrame,NsFrame*1.6));

end

function [Yq, Xq, Code, ICode] = QuantMuLawTables()  
% This routine returns quantization tables for a 256 level segmented mu-law
% quantizer.
%  Yq - 256 element quantizer output values (normalized to the interval
%    -1 to +1)
%  Xq - 255 element quantizer decision levels (normalized to the interval
%    -1 to +1)
%  Code - Coded output levels. Code(i+1) is the coded value for index i,
%    0 <= i <= 255.
%  ICode - Inverse coded levels. ICode(k+1) is the quantizer index (0 to
%    255) for coded value k.
%
% Conversion to mu-law is carried out using a quantization operation. Given
% an array of (ordered) decision levels, the interval containing the input
% value is determined. However, there is an ambiguity at the quantizer 
% decision levels. G.711 allows the output value corresponding to the
% decision levels to go either up or down. The decision levels themselves
% are symmetric with respect to zero. The ITU-T Software Tool Library (STL)
% (Recommendation G.191) has a reference implementation for which positive
% values on the decision levels move upward (away from zero) and negative
% values on the decision levels also move upward (towards zero).
%
% The present implementation uses direct quantization. For the
% quantization routine Quant with QType = 2, the intervals are defined
% as Xq(i-1) <= x < Xq(i) as in the STL reference implementation.
%
% Mu-law data is stored in sign-magnitude bit-complemented format.
%   Code = Index,        0 <= Index <= 128,
%        = 383-Index,  129 <= Index <= 255.
% The inverse mapping is
%   Index = Code+129,     0 <= Code <= 127,
%         = 256-Code,   128 <= Code <= 255.
%
% Reference implementation:
%   Index = Quant(x, Xq, 2);
%   CodedIndex = Code(Index+1);
%   ...
%   Index = ICode(CodedIndex+1);
%   xq = Yq(Index+1);

XqH = [0;
       4;     12;     20;     28;     36;     44;     52;     60;
      68;     76;     84;     92;    100;    108;    116;    124;
     140;    156;    172;    188;    204;    220;    236;    252;
     268;    284;    300;    316;    332;    348;    364;    380;
     412;    444;    476;    508;    540;    572;    604;    636;
     668;    700;    732;    764;    796;    828;    860;    892;
     956;   1020;   1084;   1148;   1212;   1276;   1340;   1404;
    1468;   1532;   1596;   1660;   1724;   1788;   1852;   1916;
    2044;   2172;   2300;   2428;   2556;   2684;   2812;   2940;
    3068;   3196;   3324;   3452;   3580;   3708;   3836;   3964;
    4220;   4476;   4732;   4988;   5244;   5500;   5756;   6012;
    6268;   6524;   6780;   7036;   7292;   7548;   7804;   8060;
    8572;   9084;   9596;  10108;  10620;  11132;  11644;  12156;
   12668;  13180;  13692;  14204;  14716;  15228;  15740;  16252;
   17276;  18300;  19324;  20348;  21372;  22396;  23420;  24444;
   25468;  26492;  27516;  28540;  29564;  30588;  31612];
YqH = [
       0;      8;     16;     24;     32;     40;      48;    56;
      64;     72;     80;     88;     96;    104;     112;   120;
     132;    148;    164;    180;    196;    212;    228;    244;
     260;    276;    292;    308;    324;    340;    356;    372;
     396;    428;    460;    492;    524;    556;    588;    620;
     652;    684;    716;    748;    780;    812;    844;    876;
     924;    988;   1052;   1116;   1180;   1244;   1308;   1372;
    1436;   1500;   1564;   1628;   1692;   1756;   1820;   1884;
    1980;   2108;   2236;   2364;   2492;   2620;   2748;   2876;
    3004;   3132;   3260;   3388;   3516;   3644;   3772;   3900;
    4092;   4348;   4604;   4860;   5116;   5372;   5628;   5884;
    6140;   6396;   6652;   6908;   7164;   7420;   7676;   7932;
    8316;   8828;   9340;   9852;  10364;  10876;  11388;  11900;
   12412;  12924;  13436;  13948;  14460;  14972;  15484;  15996;
   16764;  17788;  18812;  19836;  20860;  21884;  22908;  23932;
   24956;  25980;  27004;  28028;  29052;  30076;  31100;  32124];

% Normalize the quantizer decision levels and quantizer output levels
% Maximum output level is 32124/32768 = 0.9803
Xq = [-flipud(XqH(2:end)); XqH] / 32768;   % Includes a +zero and a -zero output
Yq = [-flipud(YqH); YqH] / 32768;

% Coded index
Code = [(0:127)'; (255:-1:128)'];
ICode = [(0:127)'; (255:-1:128)'];

end


function VADPar = InitVADPar()

% initialize constant parameters
VADPar.M = 10;     % LP order
VADPar.NP = 12;    % autocorrelation order

VADPar.N0 = 128;   % number of frames for long-term min energy calculation
VADPar.Ni = 32;    % number of frames for initialization of running averages
VADPar.INIT_COUNT = 20;

% HPFilt is a HPF that is used to preprocess the signal applied to the VAD.
% 140 Hz cutoff, unity gain near 200 Hz, falling to 0.971 at high freq.
VADPar.HPFilt.b = [ 0.92727435, -1.8544941,  0.92727435 ];
VADPar.HPFilt.a = [ 1,          -1.9059465,  0.91140240 ];
VADPar.HPFilt.Mem = [];

VADPar.N = 240;    % window size
VADPar.LA = 40;    % Look-ahead
VADPar.NF = 80;    % Frame size

LWmem = VADPar.N - VADPar.NF;
VADPar.Wmem = zeros(LWmem, 1);

LA = VADPar.LA;
LB = VADPar.N - VADPar.LA;
VADPar.Window = [0.54 - 0.46*cos(2*pi*(0:LB-1)'/(2*LB-1));
                 cos(2*pi*(0:LA-1)'/(4*LA-1))];

% LP analysis, lag window applied to autocorrelation coefficients
Fs = 8000;
BWExp = 60;         % 60 Hz bandwidth expansion, Gaussian window
w0 = 2 * pi * BWExp / Fs;
NP = VADPar.NP;
Wn = 1.0001;        % White noise compensation (diagonal loading)
VADPar.LagWindow = [Wn; exp(-0.5 * (w0 * (1:NP)').^2)] / Wn;

% Correlation for a lowpass filter (3 dB point on the power spectrum is
% at about 2 kHz)
VADPar.LBF_CORR = ...
    [ 0.24017939691329, 0.21398822343783, 0.14767692339633, ...
      0.07018811903116, 0.00980856433051,-0.02015934721195, ...
     -0.02388269958005,-0.01480076155002,-0.00503292155509, ...
      0.00012141366508, 0.00119354245231, 0.00065908718613, ...
      0.00015015782285]';

% initialize variable parameters
VADPar.FrmCount = 0;
VADPar.FrmEn = Inf * ones(1,VADPar.N0);
VADPar.MeanLSF = zeros(VADPar.M, 1);
VADPar.MeanSE = 0;
VADPar.MeanSLE = 0;
VADPar.MeanE = 0;
VADPar.MeanSZC = 0;
VADPar.count_sil = 0;
VADPar.count_inert = 0;     % modified for AppendixII
VADPar.count_update = 0;
VADPar.count_ext = 0;
VADPar.less_count = 0;
VADPar.flag = 1;

VADPar.PrevMarkers = [1, 1];
VADPar.PrevEnergy = 0;

VADPar.Prev_MinE = Inf;
VADPar.Next_MinE = Inf;
VADPar.MinE_buffer = Inf * ones(1, VADPar.N0/8);

end

function [Ivd, VADPar, v_flag] = VAD (x_new, VADPar)
% The Matlab routine implements the Voice Activity Detector (VAD) for
% the ITU-T G.729 coder. The VAD is specified in G.729B (annex B to
% G.729) to accompany G.729A the low complexity version of the G.729 coder.
% There is a modification to the VAD given in Appendix II (G.729II).
%
% The reference code for G.729A, G.729B, and G.729II uses fixed point
% arithmetic. However, G.729C+ includes reference code in floating point
% for both the coder and the VAD. This Matlab routine in double precision
% floating point borrows the relevant parts from the Annex C+ floating
% point code, but retains the decision logic of Appendix II. A switch is
% available to disable the Appendix II modifications.

% The VAD uses the preprocessed speech (highpass filtered) and the linear
% predictive parameters from the coder. The Matlab code here is standalone
% and so includes the preprocessing and the LP analysis.

% Tests on this VAD show a match to the G.729C+ VAD decisions (with the
% Appendix II option turned off).

% P. Kabal 2008-04-03

% Ivd - VAD flag, 0 no speech, 1 speech
% VADPar - Updated parameter structure
% v_flag - one during hangover (only for VAD_APPENDIX_II = 0)

VAD_APPENDIX_II = 1;

% Constants
N = VADPar.N;    % window size
N0 = VADPar.N0;  % number of frames used for long-term minimum energy calculation
Ni = VADPar.Ni;  % number of frames used for initialization of running averages
INIT_COUNT = VADPar.INIT_COUNT;
NOISE = 0;
VOICE = 1;
v_flag = 0;

VADPar.FrmCount = VADPar.FrmCount + 1;
frm_count = VADPar.FrmCount;

% Filter new data (HP filter)
[x_new_hp, VADPar.HPFilt.Mem] = filter(VADPar.HPFilt.b, VADPar.HPFilt.a, ...
                                       32768 * x_new, VADPar.HPFilt.Mem);

% Append new filtered data to filter memory
xwin = [VADPar.Wmem; x_new_hp];

% LPC analysis
[r, LSF, rc2] = VADLPAnalysis(xwin, VADPar);

% Full band energy
Ef = 10*log10(r(1) / N);

% Low band energy
Elow = r(1) * VADPar.LBF_CORR(1) ...
       + 2 * sum(r(2:end) .* VADPar.LBF_CORR(2:end));
El = 10*log10(Elow / N);

% Compute SD
SD = sum((LSF-VADPar.MeanLSF).^2);

% Normalized zero-crossing rate (in current frame)
ist = VADPar.N - VADPar.LA - VADPar.NF + 1;     % Current frame start
ifn = ist + VADPar.NF - 1;                      % Current frame end
ZC = zcr(xwin(ist:ifn+1));

% The next steps involve finding the minimum energy in the N0 frames.
% The original code in G.729 is very convoluted. The Matlab code below
% mimics the operation with a simpler structure.
% - To reduce computations, the minimum energy for blocks of 8 samples
%   is determined. These values are stored in a buffer of length N0/8.
%   The buffer is updated whenever the frame count is a multiple of 8.
%   Starting at the beginning, the minimum of the frames 1-8 is stored
%   into the buffer in frame 8, the minimum of the frames 9-16 is stored
%   into the buffer at frame 16, etc.
% - Prev_Min is the minimum of the values stored in the buffer, effectively
%   the minimum of N0 energy values.
% - Next_Min is the minimum used to determine the minimum of the next
%   8 samples.
% - MinE is min(Prev_Min, Next_Min).
% - Note that that for frame count equal to a multiple of 8, Next_Min is
%   updated and MinE is updated before updating the buffer. This means
%   that MinE is calculated over N0+8 values. MinE is effectively
%   calculated over a varying window length (N0+1 to N0+8). It is
%   nonincreasing while the window length increases.
% - The value of Min will not be used until frame N0.

% Long-term minimum energy
VADPar.Next_MinE = min(Ef, VADPar.Next_MinE);
MinE = min(VADPar.Prev_MinE, VADPar.Next_MinE);
if (mod(frm_count, 8) == 0)
  VADPar.MinE_buffer = [VADPar.MinE_buffer(2:end), VADPar.Next_MinE];
  VADPar.Prev_MinE = min(VADPar.MinE_buffer);
  VADPar.Next_MinE = Inf;
end

% Initialization of running averages
if (frm_count <= Ni)
  if (Ef < 21)
    VADPar.less_count = VADPar.less_count + 1;
    marker = NOISE;
  else
    marker = VOICE;
    NEp = (frm_count - 1) - VADPar.less_count;
    NE = NEp + 1;
    VADPar.MeanE = (VADPar.MeanE * NEp + Ef) / NE;
    VADPar.MeanSZC = (VADPar.MeanSZC * NEp + ZC) / NE;
    VADPar.MeanLSF = (VADPar.MeanLSF * NEp + LSF) / NE;
  end

end
 
if (frm_count >= Ni)
  if (frm_count == Ni)
    if (VAD_APPENDIX_II)
      if (VADPar.less_count >= Ni)    % modified for Appendix II
        VADPar.FrmCount = 0;
        frm_count = VADPar.FrmCount;
        VADPar.less_count = 0;
      end
    end
    VADPar.MeanSE = VADPar.MeanE - 10;
    VADPar.MeanSLE = VADPar.MeanE - 12;
  end

  dSE = VADPar.MeanSE - Ef;
  dSLE = VADPar.MeanSLE - El;
  dSZC = VADPar.MeanSZC - ZC;

  if (Ef < 21)
    marker = NOISE;
  else
    marker = MakeDec(dSLE, dSE, SD, dSZC);
  end

  if (VAD_APPENDIX_II)
    if (marker == VOICE)             % modified for Appendix II
      VADPar.count_inert = 0;
    end

    if (marker == NOISE && VADPar.count_inert < 6)
      VADPar.count_inert = VADPar.count_inert + 1;
      marker = VOICE;
    end
  else
    v_flag = 0;
  end
    
% Voice activity decision smoothing: Step 1
  if (VADPar.PrevMarkers(1) == VOICE && marker == NOISE ...
      && Ef > VADPar.MeanSE + 2 && Ef > 21)
    marker = VOICE;
    if (~VAD_APPENDIX_II)
      v_flag = 1;
    end
  end
    
    % Voice activity decision smoothing: Step 2
  if (VADPar.flag == 1)
    if (VADPar.PrevMarkers(2) == VOICE ...
        && VADPar.PrevMarkers(1) == VOICE ...
        && marker == NOISE ...
        && abs(Ef - VADPar.PrevEnergy) <= 3)
      VADPar.count_ext = VADPar.count_ext + 1;
      marker = VOICE;
      if(~ VAD_APPENDIX_II)
        v_flag = 1;
      end
            
      if (VADPar.count_ext <= 4)
        VADPar.flag = 1;
      else
        VADPar.flag = 0;
        VADPar.count_ext = 0;
      end
    end
  else
    VADPar.flag = 1;
  end

% For unvoiced case, count_sil is incremented
  if (marker == NOISE)
     VADPar.count_sil = VADPar.count_sil + 1;
  end
  
% Voice activity decision smoothing: Step 3    
  if (marker == VOICE && VADPar.count_sil > 10 ...
      && Ef - VADPar.PrevEnergy <= 3)
    marker = NOISE;
    VADPar.count_sil = 0;
    if (VAD_APPENDIX_II)
       VADPar.count_inert = 6;  % modified for AppendixII
    end
  end
    
  if (marker == VOICE)
    VADPar.count_sil = 0;
  end

% Voice activity decision smoothing: Step 4
  if (~VAD_APPENDIX_II)
    if (Ef < VADPar.MeanSE + 3 && VADPar.FrmCount > N0 ...
        && v_flag == 0 && rc2 < 0.6)
      marker = NOISE;
    end
  end

  if (VAD_APPENDIX_II)
    TestC = (Ef < VADPar.MeanSE + 3 && rc2 < 0.75);       % Appendix II
  else
    TestC = (Ef < VADPar.MeanSE + 3 && rc2 < 0.75 && SD < 0.002532959);
  end
  if (TestC)
    VADPar.count_update = VADPar.count_update + 1;
    % Modify update speed coefficients
    if (VADPar.count_update < INIT_COUNT)
      COEF = 0.75;
      COEFZC = 0.8;
      COEFSD = 0.6;
    elseif (VADPar.count_update < INIT_COUNT + 10)
      COEF = 0.95;
      COEFZC = 0.92;
      COEFSD = 0.65;
    elseif (VADPar.count_update < INIT_COUNT + 20)
      COEF = 0.97;
      COEFZC = 0.94;
      COEFSD = 0.70;
    elseif (VADPar.count_update < INIT_COUNT + 30)
      COEF = 0.99;
      COEFZC = 0.96;
      COEFSD = 0.75;
    elseif (VADPar.count_update < INIT_COUNT + 40)
      COEF = 0.995;
      COEFZC = 0.99;
      COEFSD = 0.75;
    else
      COEF = 0.995;
      COEFZC = 0.998;
      COEFSD = 0.75;
    end

% Update mean LSF, SE, SLE, SZC
    VADPar.MeanLSF = COEFSD * VADPar.MeanLSF + (1-COEFSD) * LSF;
    VADPar.MeanSE = COEF * VADPar.MeanSE + (1-COEF) * Ef;
    VADPar.MeanSLE = COEF * VADPar.MeanSLE + (1-COEF) * El;
    VADPar.MeanSZC = COEFZC * VADPar.MeanSZC + (1-COEFZC) * ZC;
  end

  if (frm_count > N0 && ...
        (VADPar.MeanSE < MinE && SD < 0.002532959) ...
          || VADPar.MeanSE > MinE + 10 )
    VADPar.MeanSE = MinE;
    VADPar.count_update = 0;
  end
end

VADPar.PrevEnergy = Ef;
VADPar.PrevMarkers = [marker, VADPar.PrevMarkers(1)];

ist = VADPar.NF + 1;
VADPar.Wmem = xwin(ist:end);

Ivd = marker;

end
 
 % ----- ----- ----- -----
function dec = MakeDec(dSLE, dSE, SD, dSZC)

a = [0.00175, -0.004545455, -25, 20, 0, ...
     8800, 0, 25, -29.09091, 0, ...
     14000, 0.928571, -1.5, 0.714285];

b = [0.00085, 0.001159091, -5, -6, -4.7, ...
     -12.2, 0.0009, -7.0, -4.8182, -5.3, ...
     -15.5, 1.14285, -9, -2.1428571];

dec = 0;

% SD vs dSZC
if SD > a(1)*dSZC+b(1)
    dec = 1;
    return;
end

if SD > a(2)*dSZC+b(2)
    dec = 1;
    return;
end

% dSE vs dSZC
if dSE < a(3)*dSZC+b(3)
    dec = 1;
    return;
end

if dSE < a(4)*dSZC+b(4)
    dec = 1;
    return;
end

if dSE < b(5)
    dec = 1;
    return;
end
    
% dSE vs SD       
if dSE < a(6)*SD+b(6)
    dec = 1;
    return;
end

if SD > b(7)
    dec = 1;
    return;
end

% dSLE vs dSZC
if dSLE < a(8)*dSZC+b(8)
    dec = 1;
    return;
end

if dSLE < a(9)*dSZC+b(9)
    dec = 1;
    return;
end

if dSLE < b(10)
    dec = 1;
    return;
end

% dSLE vs SD
if dSLE < a(11)*SD+b(11)
    dec = 1;
    return;
end

% dSLE vs dSE
if dSLE > a(12)*dSE+b(12)
    dec = 1;
    return
end

if dSLE < a(13)*dSE+b(13)
    dec = 1;
    return;
end

if dSLE < a(14)*dSE+b(14)
    dec = 1;
    return;
end

end

% ----- ----- ----- -----
function [zc] = zcr (x)
% Calculate normalized (per sample) zero-crossing rate
% Input is the frame plus the first sample of the next
% frame.

M = length(x) - 1;

x1 = x(1:end-1);
x2 = x(2:end);

xp = x1 .* x2;
I = (xp < 0);

%sign1 = sign(x);
%sign2 = sign([mem; x(1:M-1)]);
%
%zc = 1/(2*M)*sum(abs(sign1-sign2));

zc = sum(I) / M;

end

% -----------------------------
function [r, LSF, rc2] = VADLPAnalysis (x, VADPar)

M = VADPar.M;    % LP order
NP = VADPar.NP;  % autocorrelation order

% Apply window to input frame
xw = VADPar.Window .* x;

% Compute autocorrelation
r = acorr(xw, NP+1) .* VADPar.LagWindow;

% Compute normalized LSF
A = ac2poly(r(1:M+1));
LSF = poly2lsf(A) / (2 * pi);    % normalized to 0 to 0.5

% Reflection coefficients
rc = ac2rc(r(1:3));
rc2 = rc(2);

end

% -----------------------------
function rxx = acorr (x, Nt)

Nx = length (x);
N = Nt;
if (Nt > Nx)
  N = Nx;
end

rxx = zeros(Nt, 1);
for (i = 0:N-1)
  Nv = Nx - i;
  rxx(i+1) = x(1:Nv)' * x(i+1:i+Nv);
end

end
