close all
clear all

load extracted_data;

rtt_comb = zeros(100,3);
jBsize_comb = zeros(100,3);
TURN = zeros(100,1);
matches = 1;

id_matched = zeros(1,length(session_ids(:,1)));


for i = 1 : length(session_ids(:,1))
    if id_matched(i) == 0
        sid = session_ids(i,:);
        for j = 1 : length(session_ids(:,1))
            if j ~= i && met(i,1) > -1 && met(j,1) > -1 && id_matched(j) == 0 
                err = sid - session_ids(j,:);
                e = err * err';
      
                if( e == 0)
                    sid
                    session_ids(j,:)
                    fprintf('found matching session_ids = %d with %d \n', i, j);
                    id_matched(j) = 1;
                    id_matched(i) = 1;
                    %rtt(j,:)
                    %rtt(i,:)
                    %jBsize(j,:)
                    %jBsize(i,:)
                    rtt_comb(matches,1) = (rtt(j,1) + rtt(i,1))/2;
                    rtt_comb(matches,2) = max([rtt(j,2),rtt(i,2)]);
                    rtt_comb(matches,3) = min([rtt(j,3),rtt(i,3)]);                    
                    
                    jBsize_comb(matches,:) = jBsize(j,:) + jBsize(i,:);
                    
                    if( met(i,1) == 3)
                        if(met(j,1) == 3)
                            TURN(matches) = 2;
                        else
                            TURN(matches) = 1;
                        end
                    else
                        if(met(j,1) == 3)
                            TURN(matches) = 1;
                        else
                            TURN(matches) = 0;
                        end
                    end
                    
                    %met(j,:)
                    %met(i,:)
                    
                    matches = matches + 1;
                    %pause
                end     
            end
        end
    end
    if id_matched(i) == 0
       fprintf('could not find a match for %s \n', session_ids(i,:)); 
    end
end

bins = [0 : 50 : 2500];
D = rtt_comb(:,1);% + jBsize_comb(:,1);
H = hist(D,bins);
H = H / sum(H);
figure; plot(bins,cumsum(H)*100)
grid on
xlabel('rtt Nw + Jb (ms)')
ylabel('cummulative frequency')
D = rtt_comb(:,1);
H = hist(D,bins);
figure; plot(bins,cumsum(H))
H = H / sum(H);
figure; plot(bins,cumsum(H))
figure; plot(bins,cumsum(H)*100)
grid on
xlabel('rtt Nw (ms)')
ylabel('cummulative frequency')
D = jBsize_comb(:,1);
H = hist(D,bins);
figure; plot(bins,cumsum(H))
H = H / sum(H);
figure; plot(bins,cumsum(H))
figure; plot(bins,cumsum(H)*100)
grid on
xlabel('rtt Nw (ms)')
ylabel('cummulative frequency')

idx = find(TURN == 0);
H0 = hist(D(idx),bins);
H0 = H0 / sum(H0);
figure; 
plot(bins,cumsum(H)*100,'k')
hold
plot(bins,cumsum(H0)*100)
grid on
idx = find(TURN == 1);
H1 = hist(D(idx),bins);
H1 = H1 / sum(H1);
plot(bins,cumsum(H1)*100,'r')
idx = find(TURN == 2);
H2 = hist(D(idx),bins);
H2 = H2 / sum(H2);
plot(bins,cumsum(H2)*100,'g')

