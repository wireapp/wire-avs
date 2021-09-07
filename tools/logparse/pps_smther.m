function pps = pps_smther(time)

pps = zeros(size(time));
for i = 1 : length(time)
idx = find( (time(i)-1) < time);% < (time(i)+1));
t = time(idx);
idx = find( (time(i)+1) > t);

%time(i)
%t(idx)

%if(time(i) > 26)
%    pause
%end
if( idx(end) < length(t))
pps(i) = length(idx)/( (t(idx(end)+1)) - (t(idx(1))));
elseif(i > 1)
pps(i) = pps(i-1);
else
pps(i) = 0;
end
end