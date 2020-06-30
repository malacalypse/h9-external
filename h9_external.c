/**
	@file
	h9_external - An Eventide H9 control object as a Max external
	daniel collins - malacalypse@argx.net

	@ingroup	malacalypse
*/

#include "ext.h"							// standard Max include, always required
#include "ext_obex.h"						// required for new style Max object

#include "h9.h"

////////////////////////// object struct

typedef struct _h9_external {
    t_object    ob;
    t_symbol    *name;

    void *m_outlet_sysex;
    void *m_outlet_enabled;

    // libh9 object
    h9* h9;
} t_h9_external;

///////////////////////// function prototypes
//// standard set
void *h9_external_new(t_symbol *s, long argc, t_atom *argv);
void h9_external_free(t_h9_external *x);
void h9_external_assist(t_h9_external *x, void *b, long m, long a, char *s);

void h9_external_bang(t_h9_external *x);
void h9_external_identify(t_h9_external *x);
void h9_external_set(t_h9_external *x, t_symbol *s, long ac, t_atom *av);
void h9_external_int(t_h9_external *x, long n);
void h9_external_list(t_h9_external *x, t_symbol *msg, long argc, t_atom *argv);

//////////////////////// global class pointer variable
void *h9_external_class;

void ext_main(void *r)
{
	t_class *c;

	c = class_new("h9_external", (method)h9_external_new, (method)h9_external_free, (long)sizeof(t_h9_external),
				  0L /* leave NULL!! */, A_GIMME, 0);

    // Declare the responding methods for various type handlers
	class_addmethod(c, (method)h9_external_bang,	 "bang", 0);
	class_addmethod(c, (method)h9_external_int,		 "int",		A_LONG, 0);
	class_addmethod(c, (method)h9_external_identify, "identify", 0);
    class_addmethod(c, (method)h9_external_set,      "set", A_GIMME, 0);
    class_addmethod(c, (method)h9_external_list,     "list", A_GIMME, 0);
    CLASS_METHOD_ATTR_PARSE(c, "identify", "undocumented", gensym("long"), 0, "1");

	/* you CAN'T call this from the patcher */
	class_addmethod(c, (method)h9_external_assist,			"assist",		A_CANT, 0);

	CLASS_ATTR_SYM(c, "name", 0, t_h9_external, name);

	class_register(CLASS_BOX, c);
	h9_external_class = c;
}

void *h9_external_new(t_symbol *s, long argc, t_atom *argv)
{
    t_h9_external *x = NULL;

    if ((x = (t_h9_external *)object_alloc(h9_external_class))) {
        x->name = gensym("");
        if (argc && argv) {
            x->name = atom_getsym(argv);
        }
        if (!x->name || x->name == gensym(""))
            x->name = symbol_unique();

        x->m_outlet_enabled = intout((t_object *)x);
        x->m_outlet_sysex = listout((t_object *)x);

        // Init the zero state of the object
        x->h9 = h9_new();
        if(x->h9 == NULL) {
            h9_external_free(x);
            object_free(x);
            x = NULL;
        }
    }

    return (x);
}


void h9_external_assist(t_h9_external *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { //inlet
        switch(a) {
            case 0:
                sprintf(s, "I am the MASTER");
                break;
            default:
                sprintf(s, "I am inlet %ld", a);
        }
	}
	else {	// outlet
        switch(a) {
            case 0:
                sprintf(s, "I am outlet ZERO");
                break;
            default:
                sprintf(s, "I am outlet %ld", a);
        }
	}
}

void h9_external_free(t_h9_external *x)
{

}

// Input handlers for each message

void h9_external_int(t_h9_external *x, long n)
{
    object_post((t_object *)x, "Processing int %ld", n);
}

void h9_external_list(t_h9_external *x, t_symbol *msg, long argc, t_atom *argv) {
    char list[argc];
    long i = 0;
    for (; i < argc; i++) {
        if (atom_gettype(&argv[i]) != A_LONG) {
            object_post((t_object *)x, "List contains non-integer values, refusing to parse further.");
            return;
        }
        long value = atom_getlong(&argv[i]);
        if (value > UINT8_MAX) {
            object_post((t_object *)x, "List does not contain character-value integers, refusing to parse further.");
            return;
        }
        list[i] = (char)value;
    }
    object_post((t_object *)x, "Received list of %d characters.", i);
    if (h9_load(x->h9, (uint8_t *)list, i) == kH9_OK) {
        object_post((t_object *)x, "Successfully loaded preset %s.", x->h9->preset->name);
    } else {
        object_post((t_object *)x, "Not a preset, ignored.");
    }

}

void h9_external_set(t_h9_external *x, t_symbol *s, long argc, t_atom *argv)
{
    long i;
    t_atom *ap;
    t_symbol *sym;

    // Quick little tool to help decipher the arguments
    post("message selector is %s",s->s_name);
    post("there are %ld arguments",argc);
    // increment ap each time to get to the next atom
    for (i = 0, ap = argv; i < argc; i++, ap++) {
        switch (atom_gettype(ap)) {
            case A_LONG:
                post("%ld: %ld",i+1,atom_getlong(ap));
                break;
            case A_FLOAT:
                post("%ld: %.2f",i+1,atom_getfloat(ap));
                break;
            case A_SYM:
                sym = atom_getsym(ap);
                post("%ld: %s",i+1, sym->s_name);
                if (sym == gensym("xyzzy")) {
                    object_post((t_object *)x, "A hollow voice says 'Plugh'");
                }
                break;
            default:
                post("%ld: unknown atom type (%ld)", i+1, atom_gettype(ap));
                break;
        }
    }
}

void h9_external_bang(t_h9_external *x)
{
    object_post((t_object *)x, "%s says \"Bang!\"", x->name->s_name);
    uint8_t sysex_buffer[1000];
    t_atom list[1000];
    size_t bytes_written = h9_dump(x->h9, sysex_buffer, 1000, true);
    if (bytes_written > 0) {
        for (size_t i = 0; i < bytes_written; i++) {
            atom_setlong(&list[i], sysex_buffer[i]);
        }
        outlet_list(x->m_outlet_sysex, gensym("sysex"), bytes_written, list);
    }
}

void h9_external_identify(t_h9_external *x)
{
	object_post((t_object *)x, "Hello, my name is %s", x->name->s_name);
}
