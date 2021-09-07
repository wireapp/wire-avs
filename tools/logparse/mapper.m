function [map, ids_sorted, ids_count] = mapper(ids)

% hash map from id -> indices
map = containers.Map;
for k = 1:length(ids)
    id = ids{k};
    if isKey(map, id)
        map(id) = [map(id), k];
    else
        map(id) = k;
    end
end

remove(map, 'ffffffff-ffff-ffff-ffff-ffffffffffff');

% sort by number of occurences
ids_sorted = keys(map);
ids_count = zeros(length(ids_sorted), 1);
for k = 1:length(ids_sorted)
    ids_count(k) = length(map(ids_sorted{k}));
end
[ids_count, ind] = sort(ids_count, 'descend');
ids_sorted = ids_sorted(ind);

