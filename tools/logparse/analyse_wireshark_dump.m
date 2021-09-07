close all
clear all

fid = fopen('ios_edge_through_android_hotspot4.txt','rt');
str = fread(fid, '*char')';
fclose(fid);

% Wire settings
fs = 48000;
pt_target = 111;
% Whatsapp settings
%fs = 16000;
%pt_target = 120;
% facebook messenger settings
%fs = 16000;
%pt_target = 101;
% facetime
%fs = 24000;
%pt_target = [104,119,13];

ix = strfind(str, 'No. ');

time_buf = [];
pt_buf = [];
seq_buf = [];
ts_buf = [];
src_buf = [];
dst_buf = [];
src_port_buf = [];
dst_port_buf = [];
nBytes_buf = [];
msg_buf = [];
attr_type_buf = [];
attr_length_buf = [];
len_buf = [];
toc_buf = [];

for i = 1 : length(ix)-1
    tmp_str = str(ix(i) : ix(i+1)-1);

    % Get the time and srcs
    ix0 = strfind(tmp_str, 'Info');
    if length(ix0) > 0
        a = textscan(tmp_str(strfind(tmp_str, 'Info')+5:end),'%d%f%s%s%s%d');
        
        time = a{2};
        src = a{3};
        dst = a{4};
        protocol = a{5};
        len = a{6};
        
        ix0 = strfind(tmp_str,'Src Port:');
        ix00 = strfind(tmp_str(ix0:end),'(');
        ix01 = strfind(tmp_str(ix0:end),')');
        tmp_str0 = tmp_str(ix0:end);
        if length(tmp_str0) > 0
            src_port = str2num(tmp_str0(ix00(1)+1 : ix01(1)-1));
            dst_port = str2num(tmp_str0(ix00(2)+1 : ix01(2)-1));
        else
            src_port = -1;
            dst_port = -1;
        end
        ix1 = strfind(tmp_str, '0000  ');
        s = tmp_str(ix1(end)+5 : ix1(end)+5+30);
        toc = hex2dec(s(4:5));
        
        [a,b] = size(protocol);
        if a > 1 || b > 1
           protocol = 'N/A';
        end
        
        if strcmp(protocol,'UDP')
            msg_len = -1;
            msg_type = -1;
            attr_type = -1;
            attr_length = -1;
            ix0 = strfind(tmp_str,'[Length:');
            a = textscan(tmp_str(ix0+length('[Length:') : end), '%d');
            msg_len = a{1};        
            hdr_len = len - msg_len;
            if msg_len > 16 
                if hdr_len == 42
                    % Ipv4 + Ethernet
                    ix1 = strfind(tmp_str(ix0:end), '0020  ');
                    s = tmp_str(ix0+ix1+5+10*3 : ix0+ix1+5+10*3+30);
                    pt = hex2dec(s(4:5));
                    seq_nr = hex2dec([s(7:8),s(10:11)]);
                    ix2 = strfind(tmp_str(ix0:end), '0030  ');
                    s2 = tmp_str(ix0+ix2+5 : ix0+ix2+5+30);
                    timestamp = hex2dec([s(13:14),s(16:17),s2(1:2),s2(4:5)]);
                else
                    % Ipv4
                    ix1 = strfind(tmp_str(ix0:end), '0010  ');
                    s = tmp_str(ix0+ix1+5+12*3 : ix0+ix1+5+12*3+30);
                    pt = hex2dec(s(4:5));
                    seq_nr = hex2dec([s(7:8),s(10:11)]);
                    ix1 = strfind(tmp_str(ix0:end), '0020  ');
                    s = tmp_str(ix0+ix1+5 : ix0+ix1+5+30);
                    timestamp = hex2dec([s(1:2),s(4:5),s(7:8),s(10:11)]);            
                end
            else
                pt = -1;
                seq_nr = -1;
                timestamp = -1;
            end
            payload_len = double(msg_len);    
        elseif strcmp(protocol,'TFTP') || strcmp(protocol,'STUN')
            ix0 = strfind(tmp_str,'Message Length:');
            a = textscan(tmp_str(ix0+length('Message Length:') : end), '%d');
            msg_len = a{1};
            hdr_len = len - msg_len;
        
            if msg_len < 20
                msg_type = -1;
                attr_type = -1;
                attr_length = -1;
                pt = -1;
                seq_nr = -1;
                timestamp = -1;
                payload_len = 0;
            else
                if hdr_len == 62
                    % ipv4 + Ethernet
                    ix1 = strfind(tmp_str(ix0:end), '0020  ');
                    s = tmp_str(ix0+ix1+5+10*3 : ix0+ix1+5+10*3+30);
                    msg_type = hex2dec([s(1:2),s(4:5)]);            
            
                    ix1 = strfind(tmp_str(ix0:end), '0040  ');
                    s = tmp_str(ix0+ix1+5+10*3 : ix0+ix1+5+10*3+30);
                    attr_type = hex2dec([s(1:2),s(4:5)]);            
                    attr_length = hex2dec([s(7:8),s(10:11)]);
                    pt = hex2dec(s(16:17));
            
                    ix1 = strfind(tmp_str(ix0:end), '0050  ');
                    s = tmp_str(ix0+ix1+5 : ix0+ix1+5+30);            
                    seq_nr = hex2dec([s(1:2),s(4:5)]);
                    timestamp = hex2dec([s(7:8),s(10:11),s(13:14),s(16:17)]);
            
                    payload_len = double(len) - 80;            
                else
                    % ipv4
                    ix1 = strfind(tmp_str(ix0:end), '0010  ');
                    s = tmp_str(ix0+ix1+5+12*3 : ix0+ix1+5+12*3+30);
                    msg_type = hex2dec([s(1:2),s(4:5)]);
                        
                    ix1 = strfind(tmp_str(ix0:end), '0030  ');
                    s = tmp_str(ix0+ix1+5 : ix0+ix1+5+30);
                    attr_type = hex2dec([s(1:2),s(4:5)]);
                    attr_length = hex2dec([s(7:8),s(10:11)]);
        
                    ix1 = strfind(tmp_str(ix0:end), '0040  ');
                    s = tmp_str(ix0+ix1+5 : ix0+ix1+30);
                    pt = hex2dec(s(4:5));
            
                    seq_nr = hex2dec([s(7:8),s(10:11)]);
                    timestamp = hex2dec([s(13:14),s(16:17),s(19:20),s(22:23)]);
        
                    payload_len = double(len) - 64;            
                end
            end
            if strcmp(protocol,'TFTP')
                pt = -2;
            end
        else
            msg_len = -1;
            msg_type = -1;
            attr_type = -1;
            attr_length = -1;
            pt = -1;
            seq_nr = -1;
            timestamp = -1;
            payload_len = -1;
        end
                    
        if ~strcmp(protocol,'N/A') && ...
           ~strcmp(protocol,'IPv6') && ...
           ~strcmp(protocol,'ARP') && ...
           ~strcmp(protocol,'MDNS') && ...
           ~strcmp(protocol,'ICMPv6') && ...
           ~strcmp(protocol,'DHCPv6') && ...
           ~strcmp(protocol,'LLMNR')
        
            time_buf = [time_buf, time];
            pt_buf = [pt_buf, pt];
            seq_buf = [seq_buf, seq_nr];
            ts_buf = [ts_buf, timestamp];
            src_buf = [src_buf, ip2num(src{1})];
            dst_buf = [dst_buf, ip2num(dst{1})];
            src_port_buf = [src_port_buf, src_port];
            dst_port_buf = [dst_port_buf, dst_port];
            nBytes_buf =  [nBytes_buf, payload_len];
            msg_buf = [msg_buf, msg_type];
            attr_type_buf = [attr_type_buf, attr_type];
            attr_length_buf = [attr_length_buf, attr_length];
            len_buf = [len_buf, len];    
            toc_buf = [toc_buf, toc];
        end                
    end
end


flows = find_flows(src_buf, dst_buf, src_port_buf, dst_port_buf);

figure
plot_rate(time_buf, len_buf, pt_buf, dst_buf, ip2num('192.168.43.13'), pt_target, 'dst');
figure
plot_rate(time_buf, len_buf, pt_buf, src_buf, ip2num('192.168.43.13'), pt_target, 'src');

for i = 1 : length(flows)
    
    idx = find(src_buf == flows(i).src);
    idx = intersect(idx, find(dst_buf == flows(i).dst));
    idx = intersect(idx, find(src_port_buf == flows(i).src_port));
    idx = intersect(idx, find(dst_port_buf == flows(i).dst_port));

    fprintf('Src %s %d Dst %s %d \n', num2ip(flows(i).src), flows(i).src_port, num2ip(flows(i).dst), flows(i).dst_port);     
    fprintf('%d packets send from time %f to %f \n', length(idx), time_buf(idx(1)), time_buf(idx(end)));
    t = time_buf(idx(end)) - time_buf(idx(1));
    fprintf('average bitrate = %f bps average packet rate = %f p/s \n', sum(8*len_buf(idx))/t, length(idx)/t);

    dst_buf_ = dst_buf(idx);
    data_ = nBytes_buf(idx);
    pt_ = pt_buf(idx);
    seq_ = seq_buf(idx);
    timestamp_ = ts_buf(idx);
    time_ = time_buf(idx);
    msg_ = msg_buf(idx);

    idx = [];
    for j = 1 : length(pt_target)
        idx = [idx,find(pt_ == pt_target(j))];
    end
    idx = sort(idx);

    if length(idx) > 2%10
        pt_ = pt_(idx);
        data_ = data_(idx);
        seq_ = seq_(idx);
        timestamp_ = timestamp_(idx);
        time_ = time_(idx);
        msg_ = msg_(idx);

        if 0
            [seq_,idx] = sort(seq_);
            timestamp_ = timestamp_(idx);
            pt_ = pt_(idx);
            data_ = data_(idx);
            time_ = time_(idx);
        end

        figure

        subplot(5,2,1)
        plot(time_(2:end),seq_(2:end) - seq_(1:end-1))
        subplot(5,2,3)
        dts = (timestamp_(2:end) - timestamp_(1:end-1))/fs;
        dts = [dts(1),dts];
        plot(time_, dts)
        h = hist(dts, [0:0.01:0.5]);
        h = h/sum(h);
        subplot(5,2,4)
        bar([0:0.01:0.5],h)
        axis([0 0.2 0 1])
   
        pps = pps_smther(time_);
    
        subplot(5,2,2)
        plot(time_, pps)
        subplot(5,2,8)
        plot(time_, data_*8./dts)
        subplot(5,2,10)
        plot(time_,data_*8)
        grid on

        subplot(5,2,7)
        h = hist(data_*8./dts, [0:2000:100000]);
        h = h/sum(h);
        bar([0:2000:100000],h)
        axis([0 100000 0 0.5])
        grid on

        subplot(5,2,9)
        h = hist(data_*8, [0:100:5000]);
        h = h/sum(h);
        bar([0:100:5000],h)
        axis([0 5000 0 0.5])
        grid on

        Delay = time_ - (timestamp_/fs);
        Delay = Delay - min(Delay);
        subplot(5,2,6)
        plot(time_, Delay)
        axis([min(time_) max(time_) 0 10])
        grid on

        subplot(5,2,5)
        h = hist(Delay, [0:0.5:20]);
        h = h/sum(h);
        plot([0:0.5:20],cumsum(h))
        axis([1 20 0.9 1.0])
        grid on

        t = sprintf('Src %s %d Dst %s %d \n', num2ip(flows(i).src), flows(i).src_port, num2ip(flows(i).dst), flows(i).dst_port); 
        set(gcf,'NextPlot','add');
        axes;
        h = title(t);
        set(gca,'Visible','off');
        set(h,'Visible','on');
        
    end
end

