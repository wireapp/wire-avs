function print_logs(id1, id2)

load extracted_data;
load extracted_maps

i = get_logs(users_map, sessions_map, id1);
if nargin == 2
    ii = get_logs(users_map, sessions_map, id2);
    i = intersect(i, ii);
end

% sort by time
t = zeros(size(i));
for k = 1:length(i)
    str = start_time{i(k)};
    t(k) = posixtime(datetime(str(1:end-4), 'InputFormat', 'yyyy/MM/dd HH:mm:ss'));
    t(k) = t(k) + str2double(str(end-2:end)) / 1000;
end
[~, ii] = sort(t);
i = i(ii);

if 0
    fprintf('         Time             Session ID      User ID   Flows  Loc. Cand.  Rem. Cand.  Media Tm.  Dur.  RTT  PLoss JBSize\n');
    for k = i
        fprintf('%s  %s..%s  %s..%s   %d   %8s    %8s   %8.1f    %4d  %4d   %4d\n', ...
            start_time{k}, session_ids{k}(1:5), session_ids{k}(end-4:end), user_ids{k}(1:5), user_ids{k}(end-4:end), ...
            flows(k), rem_cand{k}, loc_cand{k}, media_time(k)/1e3, dur(k), rtt(k), jBsize(k));
    end
else
    fprintf('         Time                         Session ID                             User ID                Flows  Loc. Cand.  Rem. Cand.  Media Tm.  Dur.  RTT  PLoss JBSize\n');
    for k = i
        fprintf('%s  %s  %s   %d   %8s    %8s   %8.1f    %4d  %4d   %4d\n', ...
            start_time{k}, session_ids{k}, user_ids{k}, flows(k), rem_cand{k}, loc_cand{k}, media_time(k)/1e3, dur(k), rtt(k), jBsize(k));
    end
end
            
function log_ix = get_logs(users_map, sessions_map, id)
if isKey(users_map, id)
    disp(['User ID: ' id]);
    log_ix = users_map(id);
elseif isKey(sessions_map, id)
    disp(['Session ID: ' id]);
    log_ix = sessions_map(id);
else
    error([id, ' exists neither in the users_map nor sessions_map']);
    log_ix = [];
end
