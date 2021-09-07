function a = extract_numbers(str, key)

key = strrep(key, ' ', '\s');
key = strrep(key, '(', '\(');
key = strrep(key, ')', '\)');
pattern = ['(?<=' key '[\s\|:]+)[\d\.]+[\s\|]+[\d\.]+[\s\|]+[\d\.]+'];
substr = regexpi(str, pattern, 'match');
if isempty(substr)
    a = 0;
else
    a = sscanf(substr{1}, '%f | %f | %f')';
end
