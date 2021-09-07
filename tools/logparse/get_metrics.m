function [success, local_candidate, remote_candidate, session_id, user_id, ...
    num_flows, estab_time, media_time, setup_time] = get_metrics2(str)

local_candidate = '';
remote_candidate = '';
session_id = 'ffffffff-ffff-ffff-ffff-ffffffffffff';
user_id = 'ffffffff-ffff-ffff-ffff-ffffffffffff';
num_flows = 0;
estab_time = 0;
setup_time = 0;
media_time = 0;

str = strrep(str, '"', '');    % remove all "
success = ~isempty(strfind(str, 'success: true'));
if success
    c = regexp(str, '(?<=local_candidate:\s)\w+', 'match');
    local_candidate = c{1};
    
    c = regexp(str, '(?<=remote_candidate:\s)\w+', 'match');
    remote_candidate = c{1};
    
    ids = regexp(str, '(?<=session:\s)[\w-]+', 'match');
    if isempty(ids)
%        disp(['weird session: ' str]);
    else
        session_id = ids{1}(1:36);
        if length(ids{1}) >= 75 && ids{1}(38) == 'U'
            user_id = ids{1}(40:75);
        end
    end
    
    c = regexp(str, '(?<=num_flows:\s)\w+', 'match');
    if isempty(c)
%        disp(['weird num_flows: ' str]);
    else
        num_flows = str2double(c{1});
    end
    
    c = regexp(str, '(?<=estab_time:\s)\w+', 'match');
    if isempty(c)
        disp(['weird estab_time: ' str]);
    else
        estab_time = str2double(c{1});
    end
    
    c = regexp(str, '(?<=setup_time:\s)\w+', 'match');
    if isempty(c)
%        disp(['weird setup_time: ' str]);
    else
        setup_time = str2double(c{1});
    end
    
    c = regexp(str, '(?<=media_time:\s)\w+', 'match');
    if isempty(c)
        disp(['weird media_time: ' str]);
    else
        media_time = str2double(c{1});
    end
end
