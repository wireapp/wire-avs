
#include <re.h>

struct lock
{
};

int lock_alloc(struct lock **lp)
{
	struct lock *l;

	if (!lp)
		return EINVAL;

	l = mem_zalloc(sizeof(*l), NULL);
	if (!l)
		return ENOMEM;

	*lp = l;
	return 0;
}

void lock_read_get(struct lock *l)
{
}

void lock_write_get(struct lock *l)
{
}

int  lock_read_try(struct lock *l)
{
	return 0;
}

int  lock_write_try(struct lock *l)
{
	return 0;
}

void lock_rel(struct lock *l)
{
}

