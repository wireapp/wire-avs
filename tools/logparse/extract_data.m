close all
clear

files = dir('*.log');
%files = files(1:5000);
nFiles = length(files);

% joining/ignoring
selfJoined = zeros(nFiles, 1);
otherJoined = zeros(nFiles, 1);
selfJoinedFirst = zeros(nFiles, 1);
isIgnoringCall = zeros(nFiles, 1);

% stats
qual_reps = zeros(nFiles, 1);
dur = zeros(nFiles, 1);
ps = zeros(nFiles, 3);
rtt = zeros(nFiles, 3);
up_plr = zeros(nFiles, 3);
dn_plr = zeros(nFiles, 3);
up_jtr = zeros(nFiles, 3);
dn_jtr = zeros(nFiles, 3);
rate = zeros(nFiles, 3);
jBsize = zeros(nFiles, 3);
fec = zeros(nFiles, 3);
exp = zeros(nFiles, 3);
acc = zeros(nFiles, 3);
dcc = zeros(nFiles, 3);

% metrics
start_time = cell(nFiles, 1);
success = zeros(nFiles, 1);
flows = zeros(nFiles, 1);
estab_time = zeros(nFiles, 1);
media_time = zeros(nFiles, 1);
setup_time = zeros(nFiles, 1);
loc_cand = cell(nFiles, 1);
rem_cand = cell(nFiles, 1);
session_ids = cell(nFiles, 1);
user_ids = cell(nFiles, 1);

fid = fopen('sessions.txt','wt');
for i = 1 : length(files)
    % read log file
    fid_log = fopen(files(i).name, 'rt');
    str = fread(fid_log, '*char')';
    fclose(fid_log);
    
    start_time{i} = str(1:23);

    ix0 = strfind(str, 'self isJoined: 1');
    selfJoined(i) = ~isempty(ix0);
    ix1 = strfind(str, 'other isJoined: 1');
    otherJoined(i) = ~isempty(ix1);
    selfJoinedFirst(i) = selfJoined(i) && otherJoined(i) && ix0(1) < ix1(1);
    
    isIgnoringCall(i) = ~isempty(strfind(str, 'isIgnoringCall: 1'));
    
    ix = strfind(str, 'Receive Quality Statistics');
    qual_reps(i) = length(ix);
    
    if qual_reps(i) == 1
        % log contains one quality report
        c = regexp(str(ix:ix+100), '(?<=]\slast\s)\d+', 'match');
        dur(i) = str2double(c{1});
        
        if dur(i) > 0
            % stats
            ix_start = strfind(str(ix:min(ix+ 200, end)), 'packet size stats');
            ix_end   = strfind(str(ix:min(ix+1200, end)), '|');
            str2 = str(ix+ix_start(1)-1:ix+ix_end(end)-1);
            ps(i,:)     = extract_numbers(str2, 'packet size stats 20-40-60 ms');
            rtt(i,:)    = extract_numbers(str2, 'RTT (ms)');
            dn_jtr(i,:) = extract_numbers(str2, 'Jitter (samples)');
            up_plr(i,:) = extract_numbers(str2, 'Uplink Packet Loss');
            up_jtr(i,:) = extract_numbers(str2, 'Uplink Jitter');
            rate(i,:)   = extract_numbers(str2, 'Bitrate (kbps)');
            jBsize(i,:) = extract_numbers(str2, 'Buffer Size (ms)');
            dn_plr(i,:)    = extract_numbers(str2, 'Packet Loss Rate');
            fec(i,:)    = extract_numbers(str2, 'FEC Corrected Rate');
            exp(i,:)    = extract_numbers(str2, 'Expand Rate');
            acc(i,:)    = extract_numbers(str2, 'Accelerate Rate');
            dcc(i,:)    = extract_numbers(str2, 'Preemptive Rate');
        end
    end
    
    % metrics
    ix2 = strfind(str, 'metrics/complete');
    if ~isempty(ix2)
        ix_end = strfind(str(ix2:min(ix2+500, end)), '}');
        [success(i), rem_cand{i}, loc_cand{i}, session_ids{i}, user_ids{i}, flows(i), estab_time(i), media_time(i), setup_time(i)] = get_metrics(str(ix2(1):ix2(1)+ix_end(1)-1));
        if success(i)
            fprintf(fid, 'time: %s session_id: %s user_id: %s duration: %d remote candidate: %s local candidate: %s flows: %d \n', ...
                start_time{i}, session_ids{i}, user_ids{i}, dur(i), rem_cand{i}, loc_cand{i}, flows(i));
        end
    else
        % use this if no metrics/complete found in log (doesn't seem to happen)
        success(i) = -1;
    end
    
    if mod(i, 5000) == 0
        fprintf('cnt = %d\n', i);
    end
end
fclose all;

fprintf('\nLogs total: %d\n', nFiles);
fprintf('--> Self Joined:   %d\n', sum(selfJoined));
fprintf('--> Other Joined:  %d\n', sum(otherJoined));
fprintf('--> Both Joined:   %d\n', sum(selfJoined & otherJoined));
fprintf('----> Self Joined first: %d\n', sum(selfJoinedFirst));
fprintf('--> Ignoring Call: %d\n', sum(isIgnoringCall));
fprintf('\nLogs with 0 quality reports: %d\n', sum(qual_reps == 0));
fprintf('--> With metrics failed:  %d\n', sum(qual_reps == 0 & success == 0));
fprintf('--> With metrics success: %d\n', sum(qual_reps == 0 & success == 1));
fprintf('\nLogs with 1 quality report: %d\n', sum(qual_reps == 1));
fprintf('--> With metrics failed:  %d\n', sum(qual_reps == 1 & success == 0));
fprintf('----> With duration == 0:   %d\n', sum(qual_reps == 1 & success == 0 & dur == 0));
fprintf('----> With duration > 0:    %d\n', sum(qual_reps == 1 & success == 0 & dur > 0));
fprintf('--> With metrics success: %d\n', sum(qual_reps == 1 & success == 1));
fprintf('----> With duration == 0:   %d\n', sum(qual_reps == 1 & success == 1 & dur == 0));
fprintf('----> With duration > 0:    %d\n', sum(qual_reps == 1 & success == 1 & dur > 0));
%fprintf('------> With min(RTT) > max(RTT): %d\n', sum(qual_reps == 1 & success == 1 & dur > 0 & rtt(:, 2) < rtt(:, 3)));
fprintf('----> With num_flows == 1:  %d\n', sum(qual_reps == 1 & success == 1 & flows == 1));
fprintf('----> With num_flows > 1:   %d\n', sum(qual_reps == 1 & success == 1 & flows > 1));
fprintf('\nLogs with 2+ quality reports: %d\n', sum(qual_reps > 1));
fprintf('--> With metrics failed:  %d\n', sum(qual_reps > 1 & success == 0));
fprintf('--> With metrics success: %d\n', sum(qual_reps > 1 & success == 1));
if min(success) < 0
    fprintf('Logs without any metrics (neither success or failed): %d\n', sum(success == -1));
end



% session and user maps
[sessions_map, sessions_sorted, sessions_count] = mapper(session_ids);
[users_map, users_sorted, users_count] = mapper(user_ids);

% show details of users with most logs
fprintf('Total user IDs: %d\n', length(users_count));
for ind = 1:20
    user = users_sorted{ind};
    fprintf('User ID %s has %d logs\n', user, users_count(ind));
end

fprintf('\n');

% show details of sessions with most logs
fprintf('Total session IDs: %d\n', length(sessions_count));
for ind = 1:10
    ses = sessions_sorted{ind};
    log_ixs = sessions_map(ses);
    if ~strcmp(user_ids(log_ixs), 'ffffffff-ffff-ffff-ffff-ffffffffffff')
        fprintf('Session ID %s has %d logs:\n', ses, sessions_count(ind));
        % sort by time
        t = zeros(size(log_ixs));
        for k = 1:length(log_ixs)
            str = start_time{log_ixs(k)};
            t(k) = posixtime(datetime(str(1:end-4), 'InputFormat', 'yyyy/MM/dd HH:mm:ss'));
            t(k) = t(k) + str2double(str(end-2:end)) / 1000;
        end
        [~, i] = sort(t);
        log_ixs = log_ixs(i);
        for k = log_ixs
            fprintf('  %s user ID: %s, flows: %d, duration: %d\n', ...
                start_time{k}, user_ids{k}, flows(k), dur(k));
        end
    end
end


% indices of valid stats
ind = dur > 0 & rtt(:, 3) < rtt(:, 2) & flows == 1;

ps_ = ps(ind, :);
rtt_ = rtt(ind, :);
rate_ = rate(ind, :);
jBsize_ = jBsize(ind, :);
up_jtr_ = up_jtr(ind, :);
dn_jtr_ = dn_jtr(ind, :);
up_plr_ = up_plr(ind, :);
dn_plr_ = dn_plr(ind, :);
fec_ = fec(ind, :);
exp_ = exp(ind, :);
acc_ = acc(ind, :);
dcc_ = dcc(ind, :);
rem_cand_ = rem_cand(ind);
loc_cand_ = loc_cand(ind);

% RTT spread
bins = 0:250:10000;
H_rtt_spr = hist(rtt_(:,2) - rtt_(:,3),bins);
H_rtt_spr = H_rtt_spr / sum(H_rtt_spr);

figure
bar(bins, H_rtt_spr)
axis([-bins(2)/2 bins(end)+bins(2)/2 0 max(H_rtt_spr)*1.1])
xlabel('RTT spread (ms)')

% RTT avg / max
bins = 0:50:2000;
H_rtt_avg = hist(rtt_(:,1),bins);
H_rtt_avg = H_rtt_avg / sum(H_rtt_avg);

figure
subplot(2,1,1)
bar(bins, H_rtt_avg)
axis([-bins(2)/2 bins(end)+bins(2)/2 0 max(H_rtt_avg)*1.1])
xlabel('avg RTT (ms)')

bins = 0:250:10000;
H_rtt_max = hist(rtt_(:,2),bins);
H_rtt_max = H_rtt_max / sum(H_rtt_max);

subplot(2,1,2)
bar(bins, H_rtt_max)
axis([-bins(2)/2 bins(end)+bins(2)/2 0 max(H_rtt_max)*1.1])
xlabel('Max RTT (ms)')

% Jitter Buffer Size, avg / max
bins = 0:50:2000;
H_jbSize_avg = hist(jBsize_(:,1),bins);
H_jbSize_avg = H_jbSize_avg / sum(H_jbSize_avg);

figure
subplot(2,1,1)
bar(bins, H_jbSize_avg)
axis([-bins(2)/2 bins(end)+bins(2)/2 0 max(H_jbSize_avg)*1.1])
xlabel('Avg JB size (ms)')

bins = 0:250:10000;
H_jbSize_max = hist(jBsize_(:,1),bins);
H_jbSize_max = H_jbSize_max / sum(H_jbSize_max);

subplot(2,1,2)
bar(bins, H_jbSize_max)
axis([-bins(2)/2 bins(end)+bins(2)/2 0 max(H_jbSize_max)*1.1])
xlabel('Max JB size (ms)')

% Packet loss rate
bins = [0:1:20];
H_plr_avg = hist(dn_plr_(:,1),bins);
H_plr_avg = H_plr_avg / sum(H_plr_avg);

figure
subplot(2,1,1)
bar(bins, H_plr_avg)
axis([-bins(2)/2 bins(end)+bins(2)/2 0 1])
xlabel('Avg Packet loss rate (%)')

bins = [0:5:100];
H_plr_max = hist(dn_plr_(:,2),bins);
H_plr_max = H_plr_max / sum(H_plr_max);

subplot(2,1,2)
bar(bins, H_plr_max)
axis([-bins(2)/2 bins(end)+bins(2)/2 0 1])
xlabel('Max Packet loss rate (%)')

% Fec correction rate
bins = [0:1:20];
H_fec_avg = hist(fec_(:,1),bins);
H_fec_avg = H_fec_avg / sum(H_fec_avg);

figure
subplot(2,1,1)
bar(bins, H_fec_avg)
axis([-bins(2)/2 bins(end)+bins(2)/2 0 1])
xlabel('Avg FEC correction rate (%)')

bins = [0:5:100];
H_fec_max = hist(fec_(:,2),bins);
H_fec_max = H_fec_max / sum(H_fec_max);

subplot(2,1,2)
bar(bins, H_fec_max)
axis([0 max(bins) 0 1.0])
xlabel('Max FEC correction rate (%)')

% Expansion rate
bins = [0:1:20];
H_exp_avg = hist(exp_(:,1),bins);
H_exp_avg = H_exp_avg / sum(H_exp_avg);

figure
subplot(2,1,1)
bar(bins, H_exp_avg)
axis([-bins(2)/2 bins(end)+bins(2)/2 0 1])
xlabel('Avg Expansion rate (%)')

bins = [0:5:100];
H_exp_max = hist(exp_(:,2),bins);
H_exp_max = H_exp_max / sum(H_exp_max);

subplot(2,1,2)
bar(bins, H_exp_max)
axis([-bins(2)/2 bins(end)+bins(2)/2 0 1])
xlabel('Max Expansion rate (%)')

% Accelerate rate
bins = [0:1:20];
H_acc_avg = hist(acc_(:,1),bins);
H_acc_avg = H_acc_avg / sum(H_acc_avg);

figure
subplot(2,1,1)
bar(bins, H_acc_avg)
axis([-bins(2)/2 bins(end)+bins(2)/2 0 1])
xlabel('Avg Accelerate rate (%)')

bins = [0:5:100];
H_acc_max = hist(acc_(:,2),bins);
H_acc_max = H_acc_max / sum(H_acc_max);

subplot(2,1,2)
bar(bins, H_acc_max)
axis([-bins(2)/2 bins(end)+bins(2)/2 0 1])
xlabel('Max Accelerate rate (%)')

% Decelerate rate
bins = [0:1:20];
H_dcc_avg = hist(dcc_(:,1),bins);
H_dcc_avg = H_dcc_avg / sum(H_dcc_avg);

figure
subplot(2,1,1)
bar(bins, H_dcc_avg)
axis([-bins(2)/2 bins(end)+bins(2)/2 0 1])
xlabel('Avg Decelerate rate (%)')

subplot(2,1,2)

bins = [0:5:100];
H_dcc_max = hist(dcc_(:,2),bins);
H_dcc_max = H_dcc_max / sum(H_dcc_max);

bar(bins, H_dcc_max)
axis([-bins(2)/2 bins(end)+bins(2)/2 0 1])
xlabel('Max Decelerate rate, in 10 sec (%)')

% Metrics
ix_prflx = strcmp(rem_cand_, 'prflx');
ix_srflx = strcmp(rem_cand_, 'srflx');
ix_host  = strcmp(rem_cand_, 'host');
ix_relay = strcmp(rem_cand_, 'relay');

figure
H_met = [sum(ix_prflx), sum(ix_srflx), sum(ix_host), sum(ix_relay)] / length(rem_cand_) * 100;
bar(0:3, H_met)
axis([-0.5 3.5 0 100])
grid on
ax = gca;
ax.XTickLabel = {'prflx','srflx','host','relay'};
xlabel('Remote Candidate')

% Calculate the total delay rtt + Jb for different connection types
TotAvgDelay = jBsize_(:,1);%rtt_(:,1);% + jBsize_(:,1);
bins = [0:50:5000];
Ha = hist(TotAvgDelay,bins);
Ha = Ha / sum(Ha);
Hp = hist(TotAvgDelay(ix_prflx),bins);
Hp = Hp / sum(Hp);
Hs = hist(TotAvgDelay(ix_srflx),bins);
Hs = Hs / sum(Hs);
Hh = hist(TotAvgDelay(ix_host),bins);
Hh = Hh / sum(Hh);
Hr = hist(TotAvgDelay(ix_relay),bins);
Hr = Hr / sum(Hr);

figure
subplot(5,1,1)
bar(bins, Ha)
axis([0 2500 0 0.25])
legend('all')
subplot(5,1,2)
bar(bins, Hp)
axis([0 2500 0 0.25])
legend('prflx')
subplot(5,1,3)
bar(bins, Hs)
axis([0 2500 0 0.25])
legend('srflx')
subplot(5,1,4)
bar(bins, Hh)
axis([0 2500 0 0.25])
legend('host')
subplot(5,1,5)
bar(bins, Hr)
axis([0 2500 0 0.25])
legend('relay')
xlabel('Delay (ms)')

figure
plot(bins,cumsum(Ha),'y')
hold
plot(bins,cumsum(Hp),'r')
plot(bins,cumsum(Hs),'g')
plot(bins,cumsum(Hh),'c')
plot(bins,cumsum(Hr))
axis([0 2500 0.0 1])
grid on
legend('all','prflx','srflx','host','relay')
xlabel('Delay (ms)')

figure
plot(bins,cumsum(Ha),'y')
hold
plot(bins,cumsum(Hp),'r')
plot(bins,cumsum(Hs),'g')
plot(bins,cumsum(Hh),'c')
plot(bins,cumsum(Hr))
axis([0 5000 0.9 1.0])
grid on
legend('all','prflx','srflx','host','relay')
xlabel('Delay (ms)')

save extracted_data selfJoined selfJoinedFirst otherJoined isIgnoringCall start_time ...
    session_ids user_ids success rem_cand loc_cand flows media_time estab_time setup_time ...
    dur ps rtt rate jBsize up_plr dn_plr up_jtr dn_jtr fec exp acc dcc ...
    sessions_map sessions_sorted sessions_count users_map users_sorted users_count;

