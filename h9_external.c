/**
    @file
    h9_external - An Eventide H9 control object as a Max external
    daniel collins - malacalypse@argx.net

    @ingroup	malacalypse
*/

#include "ext.h"       // standard Max include, always required
#include "ext_obex.h"  // required for new style Max object

#include "h9.h"

////////////////////////// object struct

typedef struct _h9_external {
    t_object  ob;
    t_symbol *name;

    long proxy_num;
    void *proxy_list_controls;

    void *m_outlet_dirty;
    void *m_outlet_cc;
    void *m_outlet_sysex;
    void *m_outlet_enabled;
    void *m_outlet_knobs;     // List: [Knob num(long), Knob value(float)]
    void *m_outlet_algorithm; // List: [Module num, alg num, module name, algorithm name]

    // libh9 object
    h9 *h9;
} t_h9_external;

///////////////////////// function prototypes
//// standard set
void *h9_external_new(t_symbol *s, long argc, t_atom *argv);
void  h9_external_free(t_h9_external *x);
void  h9_external_assist(t_h9_external *x, void *b, long m, long a, char *s);

void h9_external_bang(t_h9_external *x);
void h9_external_identify(t_h9_external *x);
void h9_external_set(t_h9_external *x, t_symbol *s, long ac, t_atom *av);
void h9_external_int(t_h9_external *x, long n);
void h9_external_list(t_h9_external *x, t_symbol *msg, long argc, t_atom *argv);

// Callback handlers
void output_cc(void *ctx, uint8_t midi_channel, uint8_t cc, uint8_t msb, uint8_t lsb) {
    t_h9_external *x = (t_h9_external *)ctx;
    t_atom         list[2];
    atom_setlong(&list[0], cc);
    atom_setlong(&list[1], msb);
    outlet_list(x->m_outlet_cc, gensym("list"), 2, list);
}

void output_knobs(void *ctx, control_id control, control_value current_value, control_value display_value) {
    t_h9_external *x  = (t_h9_external *)ctx;
    // object_post((t_object *)x, "OUTPUT (knobs): %d => [%f, %f]", control, current_value, display_value);
    t_atom list[3];
    atom_setlong(&list[0], control);
    atom_setfloat(&list[1], current_value);
    atom_setfloat(&list[2], display_value);
    outlet_list(x->m_outlet_knobs, gensym("list"), 3, list);
}

void output_sysex(void *ctx, uint8_t *sysex, size_t len) {
    t_h9_external *x = (t_h9_external *)ctx;
    if (len > 0) {
        // Big atom lists of sysex might be a bit large for the stack, so ask for heap
        t_atom *list = malloc(sizeof(t_atom) * len);
        if (list == NULL) {
            object_post((t_object *)x, "Ran out of memory dumping sysex!");
            return;
        }
        for (size_t i = 0; i < len; i++) {
            atom_setlong(&list[i], sysex[i]);
        }
        outlet_list(x->m_outlet_sysex, gensym("list"), len, list);
        free(list);
    }
}

void midi_in(t_h9_external *x, long argc, t_atom *argv) {
    char  list[argc];
    long  i = 0;

    // Decide what to do by the first item in the list
    switch (atom_gettype(argv)) {
        case A_LONG:
            if (argc == 2 && atom_gettype(&argv[1]) == A_LONG) {
                // Try to treat it as CC
                long cc = atom_getlong(&argv[0]);
                long value = atom_getlong(&argv[0]);
                if ((cc > 100) || (value > 127)) {
                    object_post((t_object *)x, "INPUT (list): CC number or value are too large.");
                    return;
                }
                for (size_t i = 0; i < NUM_CONTROLS; i++) {
                    if (x->h9->midi_config.cc_tx_map[i] == (uint8_t)cc) {
                        float floatval = (float)((uint8_t)value) / 127.0f;
                        object_post((t_object *)x, "INPUT (list): CC value (%d, %d) matched control %d, setting to %f.", (uint8_t)cc, (uint8_t)value, i, floatval);
                        h9_setControl(x->h9, (control_id)i, floatval, kH9_TRIGGER_CALLBACK); // Scale 0 to 1
                        break;
                    }
                }
            } else {
                // Scan the rest to make sure it's all longs <= UINT8_MAX, and treat as sysex
                for (i = 0; i < argc; i++) {
                    if (atom_gettype(&argv[i]) != A_LONG) {
                        object_post((t_object *)x, "INPUT (list): contains non-integer values, refusing to parse further.");
                        return;
                    }
                    long value = atom_getlong(&argv[i]);
                    if (value > UINT8_MAX) {
                        object_post((t_object *)x, "INPUT (list): does not contain character-value integers, refusing to parse further.");
                        return;
                    }
                    list[i] = (char)value;
                }
                object_post((t_object *)x, "INPUT (list): Received list of %d characters.", i);
                if (h9_load(x->h9, (uint8_t *)list, i) == kH9_OK) {
                    object_post((t_object *)x, "INPUT (list): Successfully loaded preset %s.", x->h9->preset->name);
                } else {
                    object_post((t_object *)x, "INPUT (list): Not a preset, ignored.");
                }
            }
            break;
        default:
            // No clue what it is, let's just ignore it
            object_post((t_object *)x, "INPUT (list): format not recognized, ignoring.");
    }
}

void knob_in(t_h9_external *x, long argc, t_atom *argv) {
    if (argc >= 2 && atom_gettype(&argv[0]) == A_LONG && atom_gettype(&argv[1])) {
        control_id control = (control_id)atom_getlong(&argv[0]);
        control_value value = atom_getfloat(&argv[1]);
        // object_post((t_object *)x, "INPUT (knob list): setting knob %d to %f.", control, value);
        h9_setControl(x->h9, control, value, kH9_TRIGGER_CALLBACK);
    }
}

//////////////////////// global class pointer variable
void *h9_external_class;

void ext_main(void *r) {
    t_class *c;

    c = class_new("h9_external", (method)h9_external_new, (method)h9_external_free, (long)sizeof(t_h9_external), 0L /* leave NULL!! */, A_GIMME, 0);

    // Declare the responding methods for various type handlers
    class_addmethod(c, (method)h9_external_bang, "bang", 0);
    class_addmethod(c, (method)h9_external_int, "int", A_LONG, 0);
    class_addmethod(c, (method)h9_external_identify, "identify", 0);
    class_addmethod(c, (method)h9_external_set, "set", A_GIMME, 0);
    class_addmethod(c, (method)h9_external_list, "list", A_GIMME, 0);
    CLASS_METHOD_ATTR_PARSE(c, "identify", "undocumented", gensym("long"), 0, "1");

    /* you CAN'T call this from the patcher */
    class_addmethod(c, (method)h9_external_assist, "assist", A_CANT, 0);

    CLASS_ATTR_SYM(c, "name", 0, t_h9_external, name);

    class_register(CLASS_BOX, c);
    h9_external_class = c;
}

void *h9_external_new(t_symbol *s, long argc, t_atom *argv) {
    t_h9_external *x = NULL;

    if ((x = (t_h9_external *)object_alloc(h9_external_class))) {
        x->name = gensym("");
        if (argc && argv) {
            x->name = atom_getsym(argv);
        }
        if (!x->name || x->name == gensym(""))
            x->name = symbol_unique();

        x->proxy_list_controls = proxy_new((t_object *)x, 1, &x->proxy_num);

        x->m_outlet_enabled = intout((t_object *)x);
        x->m_outlet_dirty   = intout((t_object *)x);
        x->m_outlet_sysex   = listout((t_object *)x);
        x->m_outlet_cc      = listout((t_object *)x);
        x->m_outlet_knobs   = listout((t_object *)x);

        // Init the zero state of the object
        x->h9                   = h9_new();
        x->h9->cc_callback      = output_cc;
        x->h9->display_callback = output_knobs;
        x->h9->sysex_callback   = output_sysex;
        x->h9->callback_context = x;

        if (x->h9 == NULL) {
            h9_external_free(x);
            object_free(x);
            x = NULL;
        }
    }

    return (x);
}

void h9_external_assist(t_h9_external *x, void *b, long m, long a, char *s) {
    if (m == ASSIST_INLET) {  // inlet
        switch (a) {
            case 0:
                sprintf(s, "Input: list of ints = MIDI, list of floats = KNOBS");
                break;
            default:
                sprintf(s, "I am inlet %ld", a);
        }
    } else {  // outlet
        switch (a) {
            case 0:
                sprintf(s, "Control values [id, display (effective) value, base (actual) val]");
                break;
            case 1:
                sprintf(s, "CC output [cc, val]");
                break;
            case 2:
                sprintf(s, "Sysex output as list");
                break;
            case 3:
                sprintf(s, "Dirty state: 1 or 0");
                break;
            default:
                sprintf(s, "I am outlet %ld", a);
        }
    }
}

void h9_external_free(t_h9_external *x) {
    h9_delete(x->h9);
}

// Input handlers for each message

void h9_external_int(t_h9_external *x, long n) {
    object_post((t_object *)x, "Processing int %ld", n);
}

void h9_external_list(t_h9_external *x, t_symbol *msg, long argc, t_atom *argv) {
    long inlet = proxy_getinlet((t_object *)x);
    switch (inlet) {
        case 0:
            midi_in(x, argc, argv);
            break;
        case 1:
            knob_in(x, argc, argv);
            break;
        default:
            post("list received in inlet %d", inlet);
            break;
    }
}

void h9_external_set(t_h9_external *x, t_symbol *s, long argc, t_atom *argv) {
    long      i;
    t_atom *  ap;
    t_symbol *sym;

    switch (atom_gettype(argv)) {
        case A_LONG:
            post("SET: Integer %ld", atom_getlong(argv));
            break;
        case A_FLOAT:
            post("SET: Float %.2f", atom_getfloat(argv));
            break;
        case A_SYM:
            sym = atom_getsym(argv);
            if (sym == gensym("xyzzy")) {
                object_post((t_object *)x, "A hollow voice says 'Plugh'");
            } else if (sym == gensym("knobmode")) {
                if (argc >= 2) {
                    t_symbol *knobmode = atom_getsym(&argv[1]);
                    object_post((t_object *)x, "Knob Mode %s", knobmode->s_name);
                } else {
                    object_post((t_object *)x, "Ignoring blank knobmode.");
                    }
            } else if (sym == gensym("cc")) {
                // Rest should be a list [control_id cc_num]
                // TODO
            } else if (sym == gensym("id")) {
                // Second arg should be integer id
                // TODO
            } else if (sym == gensym("channel")) {
                // second arg is integer midi channel
                // TODO
            }
            break;
        default:
            post("SET: unknown atom type (%ld)", atom_gettype(ap));
            break;
    }

    // increment ap each time to get to the next atom
    for (i = 0, ap = argv; i < argc; i++, ap++) {

    }
}

void h9_external_bang(t_h9_external *x) {
    object_post((t_object *)x, "%s says \"Bang!\"", x->name->s_name);
    uint8_t sysex_buffer[1000];
    size_t  bytes_written = h9_dump(x->h9, sysex_buffer, 1000, true);
    output_sysex((void *)x, sysex_buffer, bytes_written);
}

void h9_external_identify(t_h9_external *x) {
    object_post((t_object *)x, "Hello, my name is %s", x->name->s_name);
}
