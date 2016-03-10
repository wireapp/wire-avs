% sampling rate (Hz)
Fs = 96e3;

% load samples
load_samples = 0;     % 1: load samples anew; 0: use samples loaded before
nCycles = 6;          % number of steps
nSounds = 4;          % number of variations
alph = {'A', 'B', 'C', 'D', 'E', 'F'};
if load_samples || ~exist('smpls', 'var')
    smpls = cell(nCycles, nSounds);
    path_to_samples = '';  % folder containing samples
    for m = 1:nCycles
        for k = 1:nSounds
            file_name = [path_to_samples, num2str(k), '/', alph{m}, num2str(k), '.wav'];
            disp(['loading ', file_name]);
            smpls{m}{k} = audioread(file_name);
        end
    end
end

BPM = 120;


% length per cycle
L = round(Fs / BPM * 60 * 4);

y = zeros(nCycles * L + 2*Fs, 2);
fprintf('pattern: ');
for k = 1:nCycles
    select = ceil(nSounds * rand);
    fprintf('%d', select);
    smpl = smpls{min(k, nCycles)}{select};
    t = (k-1) * L + (1:length(smpl));
    y(t, :) = y(t, :) + smpl;
end
fprintf('\n');
    

% echo
% echo delay (steps)
delay_smpls = Fs * 60 / BPM * 3 / 4;
% delay 
delay_feedback = 0.4;
delay_wet = 0.5;
% delay LP filter coefficient (closer to 0.0 means a lower cutoff)
delay_LP = 0.2;

delay_buf = zeros(delay_smpls, 2);
tmp = [0, 0];
for n = 1:length(y)
    ind = rem(n, delay_smpls) + 1;
    % mix echo with output
    ytmp = y(n, :);
    y(n, :) = y(n, :) + delay_wet * delay_buf(ind, :);
    % first order AR filter
    tmp = tmp + delay_LP * (delay_buf(ind, :) - tmp);
    % feedback loop
    delay_buf(ind, :) = ytmp + delay_feedback * tmp;
end

% compressor
y_ = y;
comp_lookahead_ms = 2;
comp_time_ms = 40;
comp_target_dB = -22;
comp_max_gain_dB = 6;
comp_ratio = 3;
comp_pwr = 4;
comp_wet = 0.5;
if comp_ratio > 1
    offset_dB = comp_target_dB - comp_max_gain_dB * comp_ratio / (comp_ratio-1);
    offset = 10^(0.05*offset_dB);
    y_lvl = sum(abs(y).^comp_pwr, 2) / size(y, 2);
    coef = 1 - 1 / (comp_time_ms/comp_pwr/1000 * Fs);
    y_lvls = filter(1-coef, [1, -coef], y_lvl) + offset^comp_pwr;
    lvl_dB = 20 / comp_pwr * log10(y_lvls);
    gain_dB = comp_target_dB - lvl_dB;
    gain_dB = gain_dB * (comp_ratio-1) / comp_ratio;
    dt = round(comp_lookahead_ms/1000 * Fs);
    gain_dB = [gain_dB(1+dt:end); ones(dt, 1) * gain_dB(end)];
    y = bsxfun(@times, y, 10.^(0.05*gain_dB));
    y = (1 - comp_wet) * y_ + comp_wet * y;
end

%spclab(Fs, y_(:, 1), y(:, 1), gain_dB)

% taper
taper_len = Fs;
y(end-taper_len+1:end, :) = bsxfun(@times, y(end-taper_len+1:end, :), linspace(1, 0, taper_len)');

% normalize output
%y = 0.7 * y / max(abs(y(:)));


% play output
if exist('obj', 'var')
    % stop playing if still not done with previous playout
    stop(obj);
end
obj = audioplayer(y, Fs);
play(obj);

