function flows = find_flows(srcs, dsts, src_ports, dst_ports)

flows = [];
%src = [];
%dst = [];
%src_port = [];
%dst_port = [];

n = 1;
vec = srcs;
Usrcs = [];
while max(vec) > 0
    Usrcs(n) = max(vec);
    vec(find(vec == max(vec))) = 0;
    n = n + 1;
end

n = 1;
vec = src_ports;
Usrc_ports = [];
while max(vec) > 0
    Usrc_ports(n) = max(vec);
    vec(find(vec == max(vec))) = 0;
    n = n + 1;
end

n = 1;
vec = dsts;
Udsts = [];
while max(vec) > 0
    Udsts(n) = max(vec);
    vec(find(vec == max(vec))) = 0;
    n = n + 1;
end

n = 1;
vec = dst_ports;
Udst_ports = [];
while max(vec) > 0
    Udst_ports(n) = max(vec);
    vec(find(vec == max(vec))) = 0;
    n = n + 1;
end

num_flows = 0;

for i = 1 : length(Usrcs)
   idx = find(srcs == Usrcs(i));
   dsts_ = dsts(idx);
   src_ports_ = src_ports(idx);
   dst_ports_ = dst_ports(idx);
   for j = 1 : length(Udsts)
        idx = find(dsts_ == Udsts(j));
        src_ports__ = src_ports_(idx);
        dst_ports__ = dst_ports_(idx);
        for k = 1 : length(Usrc_ports)
            idx = find(src_ports__ == Usrc_ports(k));
            dst_ports___ = dst_ports__(idx);
            for z = 1 : length(Udst_ports)
                idx = find(dst_ports___ == Udst_ports(z));
                if(length(idx) > 0)
                    num_flows = num_flows + 1;
                    flows(num_flows).src = Usrcs(i);
                    flows(num_flows).dst = Udsts(j);
                    flows(num_flows).src_port = Usrc_ports(k);
                    flows(num_flows).dst_port = Udst_ports(z);
                end
            end
        end
   end    
end

%for i = 1 : num_flows
%   fprintf('Src %s %d Dst %s %d \n', num2ip(flows(i).src), flows(i).src_port, num2ip(flows(i).dst), flows(i).dst_port); 
%end
