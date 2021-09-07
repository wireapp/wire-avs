function ip = num2ip(num)

fak = 2^(8*3);
d = floor(num/fak);
rem = num - d*fak;
ip = num2str(d);
for i =  1 : 3
    fak = 2^(8*(3-i));
    d = floor(rem/fak);
    rem = rem - d*fak;
    ip = strcat(ip,'.',num2str(d));
end