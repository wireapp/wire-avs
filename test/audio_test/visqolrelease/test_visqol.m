[x, fs] = audioread('shorter.wav');

x = x(1:10*fs);

% convert from 48 to 16 kHz
%x = resample(x, 1, 3); fs = fs/3;

%del_ms = 0:0.25:20;
del_ms = 0;
%resample_percentage = -5:5;
resample_percentage = 0;
mode = 'WB';

if length(del_ms) > 1
    K = length(del_ms);
    resample_percentage = resample_percentage * ones(1, K);
else
    K = length(resample_percentage);
    del_ms = del_ms * ones(1, K);
end
MOS = zeros(K, 1);
DelayMS = zeros(K, 1);

tic
for k = 1:K
    del = round(del_ms(k)*fs/1000);
    y = resample(x, 100 + resample_percentage(k), 100);
    %y = x + randn(size(y)) * 1e-4;
    if del < 0
        audiowrite('ref.wav', x, fs);
        audiowrite('test.wav', y(1-del:end), fs);
    else
        audiowrite('ref.wav', x(1+del:end), fs);
        audiowrite('test.wav', y, fs);
    end
    
    [moslqo, vnsim, debugInfo]=visqol('ref.wav','test.wav',mode); 
    disp([num2str(del_ms(k)), ' ms, ', num2str(resample_percentage(k)), ' % drift: MOSLQO = ' num2str(moslqo)]);
    MOS(k) = moslqo;
    DelayMS(k) = mean(debugInfo.patchDeltas) * 16;
end
disp(['Avg time per test: ', num2str(toc / K), ' seconds']);

if std(del_ms) > eps
    figure(3);
    plot(del_ms, DelayMS);
    xlabel('Delay (ms)');
    title('PD');
    figure(4);
    plot(del_ms, MOS);
    xlabel('Delay (ms)');
    ylabel('MOSLQO');
    title(['MOSLQO as function of delay, ', mode]);
elseif std(resample_percentage) > eps
    figure(3);
    plot(resample_percentage, DelayMS);
    xlabel('Drift %');
    title('PD');
    figure(4);
    plot(resample_percentage, MOS);
    xlabel('Drift %');
    ylabel('MOSLQO');
    title(['MOSLQO as function of clock drift, ', mode]);
end
