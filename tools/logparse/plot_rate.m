function plot_rate(time, data_len, pt, ip, ip_target, pt_audio, direction)

idx = find(ip == ip_target);
time_ = time(idx);
data_len_ = data_len(idx);

rate = zeros(size(time_));
for i = 1 : length(time_)
    idx = find( (time_(i)-1) < time_);% < (time(i)+1));
    t = time_(idx);
    d = data_len_(idx);
    idx = find( (time_(i)+1) > t);

    if( idx(end) < length(t))
        rate(i) = 8*sum(d(idx))/( (t(idx(end)+1)) - (t(idx(1))));
    elseif(i > 1)
        rate(i) = rate(i-1);
    else
        rate(i) = 0;
    end
end

arg = strcat(direction,'=',num2ip(ip_target));
title(arg);
subplot(4,1,1)
plot(time_, rate/1000)
grid on
legend('Total')
ylabel('Rate (kbps)')

idx = find(pt == -1);
time_ = time(idx);
data_len_ = data_len(idx);
ip_ = ip(idx);

idx = find(ip_ == ip_target);
time_ = time_(idx);
data_len_ = data_len_(idx);

rate = zeros(size(time_));
for i = 1 : length(time_)
    idx = find( (time_(i)-1) < time_);% < (time(i)+1));
    t = time_(idx);
    d = data_len_(idx);
    idx = find( (time_(i)+1) > t);

    if( idx(end) < length(t))
        rate(i) = 8*sum(d(idx))/( (t(idx(end)+1)) - (t(idx(1))));
    elseif(i > 1)
        rate(i) = rate(i-1);
    else
        rate(i) = 0;
    end
end

subplot(4,1,2)
plot(time_, rate/1000)
grid on
legend('TCP')
ylabel('Rate (kbps)')

idx = find(pt ~= -1);
idx = intersect(idx, find(pt ~= pt_audio(1)));
for j = 2 : length(pt_audio)
    idx = intersect(idx, find(pt ~= pt_audio(j)));
end
time_ = time(idx);
data_len_ = data_len(idx);
ip_ = ip(idx);

idx = find(ip_ == ip_target);
time_ = time_(idx);
data_len_ = data_len_(idx);

rate = zeros(size(time_));
for i = 1 : length(time_)
    idx = find( (time_(i)-1) < time_);% < (time(i)+1));
    t = time_(idx);
    d = data_len_(idx);
    idx = find( (time_(i)+1) > t);

    if( idx(end) < length(t))
        rate(i) = 8*sum(d(idx))/( (t(idx(end)+1)) - (t(idx(1))));
    elseif(i > 1)
        rate(i) = rate(i-1);
    else
        rate(i) = 0;
    end
end

subplot(4,1,3)
plot(time_, rate/1000)
grid on
legend('UDP msc')
ylabel('Rate (kbps)')

idx = [];
for j = 1 : length(pt_audio)
    idx = [idx,find(pt == pt_audio(j))];
end
idx = sort(idx);

time_ = time(idx);
data_len_ = data_len(idx);
ip_ = ip(idx);

idx = find(ip_ == ip_target);
time_ = time_(idx);
data_len_ = data_len_(idx);

rate = zeros(size(time_));
for i = 1 : length(time_)
    idx = find( (time_(i)-1) < time_);% < (time(i)+1));
    t = time_(idx);
    d = data_len_(idx);
    idx = find( (time_(i)+1) > t);

    if( idx(end) < length(t))
        rate(i) = 8*sum(d(idx))/( (t(idx(end)+1)) - (t(idx(1))));
    elseif(i > 1)
        rate(i) = rate(i-1);
    else
        rate(i) = 0;
    end
end

subplot(4,1,4)
plot(time_, rate/1000)
grid on
legend('UDP Audio')
xlabel('Time (s)')
ylabel('Rate (kbps)')