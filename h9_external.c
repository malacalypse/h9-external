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

typedef enum knobmode {
    kKnobMode_Normal = 0U,
    kKnobMode_ExpMin,
    kKnobMode_ExpMax,
    kKnobMode_PSW,
} knobmode;

typedef struct _h9_external {
    t_object  ob;
    t_symbol *name;

    long  proxy_num;
    void *proxy_list_controls;

    knobmode knobmode;

    // Listed Right to Left
    void *m_outlet_enabled;  // Unused for now, will be used for M4L instance syncing
    void *m_outlet_cc;       // Outputs CC as a list [CC Value]
    void *m_outlet_sysex;    // Outputs sysex dumps
    void *m_outlet_state;    // Outputs a variety of messages

    // libh9 object
    h9 *h9;
} t_h9_external;

t_class *h9_external_class;

///////////////////////// function prototypes
//// standard set
void *h9_external_new(t_symbol *s, long argc, t_atom *argv);
void  h9_external_free(t_h9_external *x);
void  h9_external_assist(t_h9_external *x, void *b, long m, long a, char *s);

void h9_external_bang(t_h9_external *x);
void h9_external_identify(t_h9_external *x);
void h9_external_int(t_h9_external *x, long n);
void h9_external_set(t_h9_external *x, t_symbol *s, long ac, t_atom *av);
void h9_external_list(t_h9_external *x, t_symbol *s, long argc, t_atom *argv);
void h9_external_get(t_h9_external *x, t_symbol *s, long argc, t_atom *argv);

static void h9_cc_callback_handler(void *ctx, uint8_t midi_channel, uint8_t cc, uint8_t msb, uint8_t lsb);
static void h9_sysex_callback_handler(void *ctx, uint8_t *sysex, size_t len);
static void h9_display_callback_handler(void *ctx, control_id control, control_value current_value, control_value display_value);

static void output_sysex(t_h9_external *x, uint8_t *sysex, size_t len);
static void output_state(t_h9_external *x, t_symbol *s, long argc, t_atom *argv);

static void input_midi(t_h9_external *x, long argc, t_atom *argv);
static void input_control(t_h9_external *x, long argc, t_atom *argv);

static void send_control(t_h9_external *x, control_id control, control_value current_value, control_value display_value);
static void send_knobmode(t_h9_external *x);
static void send_rx_cc(t_h9_external *x);
static void send_tx_cc(t_h9_external *x);
static void send_sysex_id(t_h9_external *x);
static void send_midi_channel(t_h9_external *x);
static void send_dirty(t_h9_external *x);
static void send_name(t_h9_external *x);
static void send_module(t_h9_external *x);
static void send_algorithm(t_h9_external *x);
static void send_algorithms(t_h9_external *x);
static void send_preset_name(t_h9_external *x);
static void request_device_config(t_h9_external *x);
static void request_device_program(t_h9_external *x);
static bool validate_atom_as_cc(t_atom *atom, uint8_t *cc);
static void set_midi_cc(t_h9_external *x, uint8_t *cc_map, long argc, t_atom *argv);
static void set_midi_channel(t_h9_external *x, long argc, t_atom *argv);
static void set_sysex_id(t_h9_external *x, long argc, t_atom *argv);
static void set_module(t_h9_external *x, long argc, t_atom *argv);
static void set_algorithm(t_h9_external *x, long argc, t_atom *argv);
static void set_knobmode(t_h9_external *x, long argc, t_atom *argv);
static void set_control(t_h9_external *x, long argc, t_atom *argv);
static void set_preset_name(t_h9_external *x, long argc, t_atom *argv);

// Callback handlers
static void h9_cc_callback_handler(void *ctx, uint8_t midi_channel, uint8_t cc, uint8_t msb, uint8_t lsb) {
    t_h9_external *x = (t_h9_external *)ctx;
    t_atom         list[2];
    atom_setlong(&list[0], cc);
    atom_setlong(&list[1], msb);
    outlet_list(x->m_outlet_cc, gensym("list"), 2, list);
}

static void h9_sysex_callback_handler(void *ctx, uint8_t *sysex, size_t len) {
    t_h9_external *x = (t_h9_external *)ctx;
    output_sysex(x, sysex, len);
}

static void h9_display_callback_handler(void *ctx, control_id control, control_value current_value, control_value display_value) {
    t_h9_external *x = (t_h9_external *)ctx;
    if (x->knobmode == kKnobMode_Normal) {
        send_control(x, control, display_value, current_value);
    }
}

static void output_state(t_h9_external *x, t_symbol *s, long argc, t_atom *argv) {
    size_t len = argc + 1;
    t_atom list[len];
    atom_setsym(list, s);
    for (size_t i = 0; i < argc; i++) {
        memcpy(&list[i + 1], &argv[i], sizeof(t_atom));
    }
    outlet_list(x->m_outlet_state, gensym("list"), len, list);
}

static void output_sysex(t_h9_external *x, uint8_t *sysex, size_t len) {
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

static void input_midi(t_h9_external *x, long argc, t_atom *argv) {
    char list[argc];
    long i = 0;

    // Decide what to do by the first item in the list
    switch (atom_gettype(argv)) {
        case A_LONG:
            if (argc == 2 && atom_gettype(&argv[1]) == A_LONG) {
                // Try to treat it as CC
                long cc    = atom_getlong(&argv[0]);
                long value = atom_getlong(&argv[0]);
                if ((cc > 100) || (value > 127)) {
                    object_post((t_object *)x, "INPUT (list): CC number or value are too large.");
                    return;
                }
                for (size_t i = 0; i < NUM_CONTROLS; i++) {
                    if (x->h9->midi_config.cc_tx_map[i] == (uint8_t)cc) {
                        float floatval = (float)((uint8_t)value) / 127.0f;
                        object_post((t_object *)x, "INPUT (list): CC value (%d, %d) matched control %d, setting to %f.", (uint8_t)cc, (uint8_t)value, i, floatval);
                        h9_setControl(x->h9, (control_id)i, floatval, kH9_TRIGGER_CALLBACK);  // Scale 0 to 1
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
                    send_dirty(x);
                    send_module(x);
                    send_algorithms(x);
                    send_algorithm(x);
                    send_preset_name(x);
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

static void input_control(t_h9_external *x, long argc, t_atom *argv) {
    set_control(x, argc, argv);
}

static void update_knobs(t_h9_external *x) {
    switch (x->knobmode) {
        case kKnobMode_ExpMin:
            for (size_t i = 0; i < H9_NUM_KNOBS; i++) {
                control_value exp_min;
                control_value exp_max;
                control_value psw;
                h9_knobMap(x->h9, (control_id)i, &exp_min, &exp_max, &psw);
                send_control(x, (control_id)i, exp_min, exp_max);
            }
            break;
        case kKnobMode_ExpMax:
            for (size_t i = 0; i < H9_NUM_KNOBS; i++) {
                control_value exp_min;
                control_value exp_max;
                control_value psw;
                h9_knobMap(x->h9, (control_id)i, &exp_min, &exp_max, &psw);
                send_control(x, (control_id)i, exp_max, exp_min);
            }
            break;
        case kKnobMode_PSW:
            for (size_t i = 0; i < H9_NUM_KNOBS; i++) {
                control_value exp_min;
                control_value exp_max;
                control_value psw;
                h9_knobMap(x->h9, (control_id)i, &exp_min, &exp_max, &psw);
                send_control(x, (control_id)i, psw, h9_controlValue(x->h9, (control_id)i));
            }
            break;
        default:
            x->knobmode = kKnobMode_Normal;
            for (size_t i = 0; i < H9_NUM_KNOBS; i++) {
                control_value current_value = h9_controlValue(x->h9, (control_id)i);
                control_value display_value = h9_displayValue(x->h9, (control_id)i);
                send_control(x, (control_id)i, display_value, current_value);
            }
    }
}

static void send_control(t_h9_external *x, control_id control, control_value current_value, control_value alternate_value) {
    t_atom list[3];
    atom_setlong(&list[0], control);
    atom_setfloat(&list[1], current_value);
    atom_setfloat(&list[2], alternate_value);
    output_state(x, gensym("control"), 3, list);
}

static void send_knobmode(t_h9_external *x) {
    t_atom atom;
    switch (x->knobmode) {
        case kKnobMode_ExpMin:
            atom_setsym(&atom, gensym("exp_min"));
            break;
        case kKnobMode_ExpMax:
            atom_setsym(&atom, gensym("exp_max"));
            break;
        case kKnobMode_PSW:
            atom_setsym(&atom, gensym("psw"));
            break;
        default:
            atom_setsym(&atom, gensym("normal"));
    }
    output_state(x, gensym("knobmode"), 1, &atom);
}

static void send_rx_cc(t_h9_external *x) {
    t_atom list[NUM_CONTROLS];
    for (size_t i = 0; i < NUM_CONTROLS; i++) {
        atom_setlong(&list[i], x->h9->midi_config.cc_rx_map[i]);
    }
    output_state(x, gensym("midi_rx_cc"), NUM_CONTROLS, list);
}

static void send_tx_cc(t_h9_external *x) {
    t_atom list[NUM_CONTROLS];
    for (size_t i = 0; i < NUM_CONTROLS; i++) {
        atom_setlong(&list[i], x->h9->midi_config.cc_tx_map[i]);
    }
    output_state(x, gensym("midi_tx_cc"), NUM_CONTROLS, list);
}

static void send_sysex_id(t_h9_external *x) {
    t_atom atom;
    atom_setlong(&atom, x->h9->midi_config.sysex_id);
    output_state(x, gensym("id"), 1, &atom);
}

static void send_midi_channel(t_h9_external *x) {
    t_atom atom;
    atom_setlong(&atom, x->h9->midi_config.midi_channel);
    output_state(x, gensym("id"), 1, &atom);
}

static void send_dirty(t_h9_external *x) {
    t_atom atom;
    atom_setlong(&atom, x->h9->dirty);
    output_state(x, gensym("dirty"), 1, &atom);
}

static void send_name(t_h9_external *x) {
    t_atom atom;
    atom_setsym(&atom, x->name);
    output_state(x, gensym("name"), 1, &atom);
}

static void send_preset_name(t_h9_external *x) {
    t_atom atom;
    atom_setsym(&atom, gensym(x->h9->preset->name));
    output_state(x, gensym("preset_name"), 1, &atom);
}

static void send_module(t_h9_external *x) {
    t_atom list[2];
    atom_setlong(&list[0], h9_currentModuleIndex(x->h9));
    atom_setsym(&list[1], gensym(h9_currentModuleName(x->h9)));
    output_state(x, gensym("module"), 2, list);
}

static void send_algorithm(t_h9_external *x) {
    t_atom list[2];
    atom_setlong(&list[0], h9_currentAlgorithmIndex(x->h9));
    atom_setsym(&list[1], gensym(h9_currentAlgorithmName(x->h9)));
    output_state(x, gensym("algorithm"), 2, list);
}

static void send_algorithms(t_h9_external *x) {
    h9_module *module   = h9_currentModule(x->h9);
    size_t     num_algs = module->num_algorithms;
    t_atom     module_algorithms[num_algs];
    for (size_t i = 0; i < num_algs; i++) {
        atom_setsym(&module_algorithms[i], gensym(module->algorithms[i].name));
    }
    output_state(x, gensym("algorithms"), num_algs, module_algorithms);
}

static void request_device_program(t_h9_external *x) {
    size_t  len = 128;
    uint8_t sysex[len];
    size_t  actual_len = h9_sysexGenRequestCurrentPreset(x->h9, sysex, len);
    if (actual_len < len) {
        output_sysex(x, sysex, actual_len);
    }
}

static void request_device_config(t_h9_external *x) {
    size_t  len = 128;
    uint8_t sysex[len];
    size_t  actual_len = h9_sysexGenRequestSystemConfig(x->h9, sysex, len);
    if (actual_len < len) {
        output_sysex(x, sysex, actual_len);
    }
}

static void request_device_variable(t_h9_external *x, long argc, t_atom *argv) {
    if (argc > 0 && atom_gettype(argv) == A_LONG) {
        uint16_t address = (uint16_t)atom_getlong(argv);
        h9_sysexRequestConfigVar(x->h9, address);
    }
}

static void set_device_variable(t_h9_external *x, long argc, t_atom *argv) {
    if (argc > 1 && atom_gettype(&argv[0]) == A_LONG && atom_gettype(&argv[1]) == A_LONG) {
        uint16_t address = (uint16_t)atom_getlong(&argv[0]);
        uint16_t value   = (uint16_t)atom_getlong(&argv[1]);
        h9_sysexWriteConfigVar(x->h9, (uint16_t)address, (uint16_t)value);
    }
}

static bool validate_atom_as_cc(t_atom *atom, uint8_t *cc) {
    long type = atom_gettype(atom);

    if (type == A_SYM && atom_getsym(atom) == gensym("disabled")) {
        *cc = CC_DISABLED;
    } else if (type != A_LONG) {
        return false;
    } else {
        long atom_val = atom_getlong(atom);
        if (atom_val < 0 || atom_val > 99) {
            *cc = CC_DISABLED;
        } else {
            *cc = (uint8_t)atom_val;
        }
    }
    return true;
}

static void set_control(t_h9_external *x, long argc, t_atom *argv) {
    if (argc >= 2 && atom_gettype(&argv[0]) == A_LONG && atom_gettype(&argv[1])) {
        control_id    control   = (control_id)atom_getlong(&argv[0]);
        control_value new_value = atom_getfloat(&argv[1]);

        if (control >= H9_NUM_KNOBS) {
            // It's not a knob, handle it separately
            h9_setControl(x->h9, control, new_value, kH9_TRIGGER_CALLBACK);
        } else {
            control_value exp_min;
            control_value exp_max;
            control_value psw;

            switch (x->knobmode) {
                case kKnobMode_ExpMin:
                    h9_knobMap(x->h9, control, &exp_min, &exp_max, &psw);
                    h9_setKnobMap(x->h9, control, new_value, exp_max, psw);
                    break;
                case kKnobMode_ExpMax:
                    h9_knobMap(x->h9, control, &exp_min, &exp_max, &psw);
                    h9_setKnobMap(x->h9, control, exp_min, new_value, psw);
                    break;
                case kKnobMode_PSW:
                    h9_knobMap(x->h9, control, &exp_min, &exp_max, &psw);
                    h9_setKnobMap(x->h9, control, exp_min, exp_max, new_value);
                    break;
                default:
                    h9_setControl(x->h9, control, new_value, kH9_TRIGGER_CALLBACK);
            }
        }
        send_dirty(x);
    }
}

static void set_midi_cc(t_h9_external *x, uint8_t *cc_map, long argc, t_atom *argv) {
    uint8_t list[NUM_CONTROLS];
    if (argc == 2) {
        // [control, cc]
        if (atom_gettype(&argv[0]) != A_LONG) {
            object_error((t_object *)x, "Set: control is not an integer");
            return;
        }
        long control = atom_getlong(&argv[0]);
        validate_atom_as_cc(&argv[1], &cc_map[(control_id)control]);
    } else if (argc >= NUM_CONTROLS) {
        for (size_t i = 0; i < NUM_CONTROLS; i++) {
            if (!validate_atom_as_cc(&argv[i], &list[i])) {
                return;
            }
        }
        // Now that they're valid, assign them.
        for (size_t i = 0; i < NUM_CONTROLS; i++) {
            cc_map[(control_id)i] = list[i];
        }
    } else {
        /* skip */
    }
}
static void set_sysex_id(t_h9_external *x, long argc, t_atom *argv) {
    if (argc > 0 && atom_gettype(argv) == A_LONG) {
        long id = atom_getlong(argv);
        if (id < 0 || id > 16) {
            object_error((t_object *)x, "Set: Invalid SYSEX id %d.", id);
        }
        x->h9->midi_config.sysex_id = (uint8_t)id;
    }
}

static void set_midi_channel(t_h9_external *x, long argc, t_atom *argv) {
    if (argc > 0 && atom_gettype(argv) == A_LONG) {
        long channel = atom_getlong(argv);
        if (channel < 1 || channel > 16) {
            object_error((t_object *)x, "Set: Invalid MIDI channel %d.", channel);
        }
        x->h9->midi_config.midi_channel = (uint8_t)channel;
    }
}

static void set_module(t_h9_external *x, long argc, t_atom *argv) {
    if (argc > 0 && atom_gettype(argv) == A_LONG) {
        long mod_id = atom_getlong(argv);
        if (mod_id < 0 || mod_id >= H9_NUM_MODULES) {
            object_error((t_object *)x, "Set: Bad argument for module: %d.", mod_id);
        }
        h9_setAlgorithm(x->h9, mod_id, 0);
        send_algorithms(x);
        send_algorithm(x);
        send_dirty(x);
    } else {
        object_error((t_object *)x, "Bad argument for module.");
    }
}

static void set_algorithm(t_h9_external *x, long argc, t_atom *argv) {
    if (argc > 0 && atom_gettype(argv) == A_LONG) {
        long alg_id = atom_getlong(argv);
        if (alg_id < 0 || alg_id >= h9_currentModule(x->h9)->num_algorithms) {
            object_error((t_object *)x, "Bad argument for algorithm: %d.", alg_id);
        }
        if (!h9_setAlgorithm(x->h9, h9_currentModuleIndex(x->h9), alg_id)) {
            object_error((t_object *)x, "Could not set algorithm %d for module $d (out of %d total).", alg_id, h9_currentModule(x->h9), h9_currentModule(x->h9)->num_algorithms);
        }
        send_dirty(x);
    } else {
        object_error((t_object *)x, "Bad argument for algorithm.");
    }
}

static void set_knobmode(t_h9_external *x, long argc, t_atom *argv) {
    if (argc > 0) {
        t_symbol *knobmode = atom_getsym(argv);
        if (knobmode == gensym("exp_min")) {
            x->knobmode = kKnobMode_ExpMin;
            update_knobs(x);
        } else if (knobmode == gensym("exp_max")) {
            x->knobmode = kKnobMode_ExpMax;
            update_knobs(x);
        } else if (knobmode == gensym("psw")) {
            x->knobmode = kKnobMode_PSW;
            update_knobs(x);
        } else {
            x->knobmode = kKnobMode_Normal;
            update_knobs(x);
        }
    } else {
        // Set it to normal.
        x->knobmode = kKnobMode_Normal;
        update_knobs(x);
    }
}

static void set_preset_name(t_h9_external *x, long argc, t_atom *argv) {
    if (argc > 0 && atom_gettype(argv) == A_SYM) {
        t_symbol *preset_name = atom_getsym(argv);
        h9_setPresetName(x->h9, preset_name->s_name, strnlen(preset_name->s_name, H9_MAX_NAME_LEN));
        send_preset_name(x); // Update the field to the actual name as parsed by the h9
    }
}

/* ============================ PUBLIC function definitions ======================================*/

void ext_main(void *r) {
    t_class *c;

    c = class_new("h9_external", (method)h9_external_new, (method)h9_external_free, (long)sizeof(t_h9_external), 0L /* leave NULL!! */, A_GIMME, 0);

    // Declare the responding methods for various type handlers
    class_addmethod(c, (method)h9_external_bang, "bang", 0);
    class_addmethod(c, (method)h9_external_int, "int", A_LONG, 0);
    class_addmethod(c, (method)h9_external_identify, "identify", 0);
    class_addmethod(c, (method)h9_external_set, "set", A_GIMME, 0);
    class_addmethod(c, (method)h9_external_list, "list", A_GIMME, 0);
    class_addmethod(c, (method)h9_external_get, "get", A_GIMME, 0);
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
        x->m_outlet_sysex   = listout((t_object *)x);
        x->m_outlet_cc      = listout((t_object *)x);
        x->m_outlet_state   = listout((t_object *)x);

        // Init the zero state of the object
        x->knobmode             = kKnobMode_Normal;
        x->h9                   = h9_new();
        x->h9->cc_callback      = h9_cc_callback_handler;
        x->h9->display_callback = h9_display_callback_handler;
        x->h9->sysex_callback   = h9_sysex_callback_handler;
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

// Unimplemented for now, placeholder in case we want to run a stream of integers through midi_parse
// or to treat ints in some other useful way.
// NOTE: Sysex should, in this implementation, be input as a list. Use midi_parse or max zl tools
// (see the example patcher) to capture the sysex into a list before outputting.
void h9_external_int(t_h9_external *x, long n) {
    /* skip */
}

void h9_external_list(t_h9_external *x, t_symbol *s, long argc, t_atom *argv) {
    long inlet = proxy_getinlet((t_object *)x);
    switch (inlet) {
        case 0:
            input_midi(x, argc, argv);
            break;
        case 1:
            input_control(x, argc, argv);
            break;
        default:
            post("list received in inlet %d", inlet);
            break;
    }
}

void h9_external_set(t_h9_external *x, t_symbol *s, long argc, t_atom *argv) {
    t_symbol *sym;

    switch (atom_gettype(argv)) {
        case A_LONG:
            post("SET: Integer %ld", atom_getlong(argv));
            break;
        case A_FLOAT:
            post("SET: Float %.2f", atom_getfloat(argv));
            break;
        case A_SYM:
            sym          = atom_getsym(argv);
            long    optc = argc - 1;
            t_atom *opts = &argv[1];
            if (sym == gensym("xyzzy")) {
                object_post((t_object *)x, "A hollow voice says 'Plugh'");
            } else if (sym == gensym("knobmode")) {
                set_knobmode(x, optc, opts);
            } else if (sym == gensym("midi_rx_cc")) {
                set_midi_cc(x, x->h9->midi_config.cc_rx_map, optc, opts);
            } else if (sym == gensym("midi_tx_cc")) {
                set_midi_cc(x, x->h9->midi_config.cc_tx_map, optc, opts);
            } else if (sym == gensym("id")) {
                set_sysex_id(x, optc, opts);
            } else if (sym == gensym("channel")) {
                set_midi_channel(x, optc, opts);
            } else if (sym == gensym("module")) {
                set_module(x, optc, opts);
            } else if (sym == gensym("algorithm")) {
                set_algorithm(x, optc, opts);
            } else if (sym == gensym("preset_name")) {
                set_preset_name(x, optc, opts);
            } else if (sym == gensym("system_variable")) {
                set_device_variable(x, optc, opts);
            } else {
                object_error((t_object *)x, "SET: Cannot set %s", sym->s_name);
            }
            break;
        default:
            post("SET: unknown atom type (%ld)", atom_gettype(argv));
            break;
    }
}

void h9_external_get(t_h9_external *x, t_symbol *s, long argc, t_atom *argv) {
    if (argc > 0 && atom_gettype(argv) == A_SYM) {
        t_symbol *sym = atom_getsym(argv);
        long    optc = argc - 1;
        t_atom *opts = &argv[1];
        if (sym == gensym("knobmode")) {
            send_knobmode(x);
        } else if (sym == gensym("midi_rx_cc")) {
            send_rx_cc(x);
        } else if (sym == gensym("midi_tx_cc")) {
            send_tx_cc(x);
        } else if (sym == gensym("id")) {
            send_sysex_id(x);
        } else if (sym == gensym("channel")) {
            send_midi_channel(x);
        } else if (sym == gensym("dirty")) {
            send_dirty(x);
        } else if (sym == gensym("name")) {
            send_name(x);
        } else if (sym == gensym("module")) {
            send_module(x);
        } else if (sym == gensym("algorithm")) {
            send_algorithm(x);
        } else if (sym == gensym("algorithms")) {
            send_algorithms(x);
        } else if (sym == gensym("device_config")) {
            request_device_config(x);
        } else if (sym == gensym("device_program")) {
            request_device_program(x);
        } else if (sym == gensym("preset_name")) {
            send_preset_name(x);
        } else if (sym == gensym("system_variable")) {
            request_device_variable(x, optc, opts);
        } else {
            object_post((t_object *)x, "Get: Unsupported '%s'", sym->s_name);
        }
    } else if (argc == 0) {
        char *str = s->s_name;
        object_error((t_object *)x, "Get: Cannot get %s", str);
    } else {
        // there are arguments but the first one is not a symbol
        object_error((t_object *)x, "Get: invalid syntax");
    }
}

void h9_external_bang(t_h9_external *x) {
    object_post((t_object *)x, "%s says \"Bang!\"", x->name->s_name);
    uint8_t sysex_buffer[1000];
    size_t  bytes_written = h9_dump(x->h9, sysex_buffer, 1000, true);
    h9_sysex_callback_handler((void *)x, sysex_buffer, bytes_written);
    send_dirty(x);
    send_module(x);
    send_algorithms(x);
    send_algorithm(x);
}

void h9_external_identify(t_h9_external *x) {
    object_post((t_object *)x, "Hello, my name is %s", x->name->s_name);
}
