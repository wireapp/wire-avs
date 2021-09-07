function num = ip2num(ipadr)

ix = strfind(ipadr, '.');
ix = [0,ix];

if(length(ix) > 1)
    num = 0;    
    for i =  1 : length(ix)-1
        fak = 2^(8*(length(ix)-i));
        num = num + str2num(ipadr(ix(i)+1 : ix(i+1)-1))*fak;
    end
    fak = 1;
    num = num + str2num(ipadr(ix(i+1)+1 : end))*fak;
else
    num = -1;
end
