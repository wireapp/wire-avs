
#include <re.h>
#include "avs_jzon.h"


static int _encode_odict_pretty(struct re_printf *pf, const struct odict *o,
				unsigned offset);


static int encode_entry_pretty(struct re_printf *pf,
			       const struct odict_entry *e, unsigned offset)
{
	struct odict *array;
	struct le *le;
	unsigned i, array_offset;
	int err = 0;

	if (!e)
		return 0;

	if (odict_type_isreal(e->type)) {
		err |= re_hprintf(pf, "\033[1;34m"); /* bright blue */
	}

	switch (e->type) {

	case ODICT_OBJECT:

		err |= re_hprintf(pf, "{\n");

		err |= _encode_odict_pretty(pf, e->u.odict, offset);

		for (i=0; i<offset; i++)
			err |= re_hprintf(pf, " ");
		err |= re_hprintf(pf, "}");

		break;

	case ODICT_ARRAY:
		array_offset = offset + 8;
		array = e->u.odict;
		if (!array)
			return 0;

		err = re_hprintf(pf, "[\n");

		for (le=array->lst.head; le; le=le->next) {

			const struct odict_entry *ae = le->data;

			for (i=0; i<array_offset; i++)
				err |= re_hprintf(pf, " ");

			err |= encode_entry_pretty(pf, ae, offset);

			if (le->next)
				err |= re_hprintf(pf, ",");
			err |= re_hprintf(pf, "\n");
		}

		for (i=0; i<offset; i++)
			err |= re_hprintf(pf, " ");
		err |= re_hprintf(pf, "]");
		break;

	case ODICT_INT:
		err = re_hprintf(pf, "%lld", e->u.integer);
		break;

	case ODICT_DOUBLE:
		err = re_hprintf(pf, "%f", e->u.dbl);
		break;

	case ODICT_STRING:
		err = re_hprintf(pf, "\"%H\"", utf8_encode, e->u.str);
		break;

	case ODICT_BOOL:
		err = re_hprintf(pf, "%s", e->u.boolean ? "true" : "false");
		break;

	case ODICT_NULL:
		err = re_hprintf(pf, "null");
		break;

	default:
		re_fprintf(stderr, "json: unsupported type %d\n", e->type);
		err = EINVAL;
	}

	if (odict_type_isreal(e->type))
		err |= re_hprintf(pf, "\x1b[;m");

	return err;
}


static int _encode_odict_pretty(struct re_printf *pf, const struct odict *o,
				unsigned offset)
{
	struct le *le;
	unsigned i;
	int err = 0;

	if (!o)
		return 0;

	for (le=o->lst.head; le; le=le->next) {

		const struct odict_entry *e = le->data;
		unsigned entry_offset = offset + 8;

		for (i=0; i<entry_offset; i++)
			err |= re_hprintf(pf, " ");

		err |= re_hprintf(pf, "\"%H\" : ",
				  utf8_encode, e->key);

		err |= encode_entry_pretty(pf, e, entry_offset );

		if (le->next)
			err |= re_hprintf(pf, ",");

		err |= re_hprintf(pf, "\n");
	}

	return err;
}


int jzon_encode_odict_pretty(struct re_printf *pf, const struct odict *o)
{
	int err = 0;

	err |= re_hprintf(pf, "{\n");

	err |= _encode_odict_pretty(pf, o, 0);

	err |= re_hprintf(pf, "}\n");

	return err;
}
