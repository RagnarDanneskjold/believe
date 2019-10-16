#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define GC_DEBUG
#include <gc.h>

#define BELIEVE_VERSION   "0.1"
#define BELIEVE_COPYRIGHT "2019 Lucas Vieira"
#define BELIEVE_LICENSE   "MIT"

typedef enum
{
    BEL_SYMBOL,
    BEL_PAIR,
    BEL_CHAR,
    BEL_STREAM
} BEL_TYPE;

typedef struct BEL Bel; // Forward declaration

typedef struct
{
    Bel *car;
    Bel *cdr;
} Bel_pair;

typedef int8_t Bel_char;

typedef uint64_t Bel_sym;

typedef enum BEL_STREAM_STATUS
{
    BEL_STREAM_OPEN,
    BEL_STREAM_CLOSED
} BEL_STREAM_STATUS;

typedef enum BEL_STREAM_TYPE
{
    BEL_STREAM_READ,
    BEL_STREAM_WRITE
} BEL_STREAM_TYPE;

typedef struct
{
    BEL_STREAM_STATUS  status;
    BEL_STREAM_TYPE    type;
    FILE              *raw_stream;
} Bel_stream;

// Aliased as 'Bel' before
struct BEL
{
    BEL_TYPE type;
    union {
        Bel_sym     sym;
        Bel_pair   *pair;
        Bel_char    chr;
        Bel_stream  stream;
    };
};

#define BEL_NIL   ((Bel_sym)0)
#define BEL_T     ((Bel_sym)1)
#define BEL_O     ((Bel_sym)2)
#define BEL_APPLY ((Bel_sym)3)

Bel *bel_g_nil;
Bel *bel_g_t;
Bel *bel_g_o;
Bel *bel_g_apply;

Bel *bel_g_chars;
Bel *bel_g_ins_sys;
Bel *bel_g_outs_sys;
Bel *bel_g_ins;
Bel *bel_g_outs;

Bel *bel_g_globe;
Bel *bel_g_scope;

typedef struct {
    const char **tbl;
    uint64_t     n_syms;
    uint64_t     size;
} _Bel_sym_table;

_Bel_sym_table g_sym_table;

void
Bel_sym_table_init(void)
{
    g_sym_table.n_syms = 4;
    g_sym_table.size   = 4;
    g_sym_table.tbl    =
        GC_MALLOC(g_sym_table.size * sizeof(char*));

    g_sym_table.tbl[BEL_NIL]   = "nil";
    g_sym_table.tbl[BEL_T]     = "t";
    g_sym_table.tbl[BEL_O]     = "o";
    g_sym_table.tbl[BEL_APPLY] = "apply";
}

Bel_sym Bel_sym_table_add(const char*); // Forward declaration

Bel_sym
Bel_sym_table_find(const char *sym_literal)
{
    uint64_t i;
    size_t len = strlen(sym_literal);
    for(i = 0; i < g_sym_table.n_syms; i++) {
        if(!strncmp(sym_literal, g_sym_table.tbl[i], len)) {
            return i;
        }
    }

    return Bel_sym_table_add(sym_literal);
}

Bel_sym
Bel_sym_table_add(const char *sym_literal)
{
    if(g_sym_table.n_syms == g_sym_table.size) {
        uint64_t new_size = 2 * g_sym_table.size;
        g_sym_table.tbl = GC_REALLOC(g_sym_table.tbl,
                                     new_size * sizeof(char*));
        g_sym_table.size = new_size;
    }
    g_sym_table.tbl[g_sym_table.n_syms++] = sym_literal;
    return (g_sym_table.n_syms - 1);
}

Bel*
bel_mksymbol(const char *str)
{
    Bel *ret  = GC_MALLOC(sizeof (*ret));
    ret->type = BEL_SYMBOL;
    ret->sym  = Bel_sym_table_find(str);
    return ret;
}

Bel*
bel_mkpair(Bel *car, Bel *cdr)
{
    Bel *ret  = GC_MALLOC(sizeof (*ret));
    ret->type = BEL_PAIR;
    ret->pair = GC_MALLOC(sizeof (Bel_pair));
    ret->pair->car = car;
    ret->pair->cdr = cdr;
    return ret;
}

#define bel_nilp(x) ((x->type==BEL_SYMBOL)&&(x->sym==BEL_NIL))

// TODO: Check for errors

Bel*
bel_car(Bel *p)
{
    if(bel_nilp(p))
        return bel_g_nil;
    return p->pair->car;
}

Bel*
bel_cdr(Bel *p)
{
    if(bel_nilp(p))
        return bel_g_nil;
    return p->pair->cdr;
}

uint64_t
bel_length(Bel *list)
{
    // TODO: This makes no sense if the last element
    // is not nil, AKA if we have simple pair anywhere
    Bel *itr = list;
    uint64_t len = 0;
    while(!bel_nilp(itr)) {
        len++;
        itr = bel_cdr(itr);
    }
    return len;
}

Bel*
bel_mkchar(Bel_char c)
{
    Bel *ret  = GC_MALLOC(sizeof *ret);
    ret->type = BEL_CHAR;
    ret->chr  = c;
    return ret;
}

Bel*
bel_mkstring(const char *str)
{
    size_t len = strlen(str);

    if(len == 0)
        return bel_g_nil;
    
    Bel **pairs = GC_MALLOC(len * sizeof (Bel));

    // Create pairs where CAR is a character and CDR is nil
    size_t i;
    for(i = 0; i < len; i++) {
        Bel *chr  = GC_MALLOC(sizeof *chr);
        chr->type = BEL_CHAR;
        chr->chr  = str[i];
        pairs[i]  = bel_mkpair(chr, bel_g_nil);
    }

    // Link all pairs properly
    for(i = 0; i < len - 1; i++) {
        pairs[i]->pair->cdr = pairs[i + 1];
    }

    return pairs[0];
}

char*
bel_cstring(Bel *belstr)
{
    uint64_t len = bel_length(belstr);
    if(len == 0) return NULL;
    
    char *str    = GC_MALLOC((len + 1) * sizeof (*str));

    Bel *itr     = belstr;
    size_t i     = 0;
    while(!bel_nilp(itr)) {
        str[i] = bel_car(itr)->chr;
        itr    = bel_cdr(itr);
        i++;
    }
    str[i] = '\0';
    return str;
}

Bel*
bel_mkstream(const char* name, BEL_STREAM_TYPE type)
{
    Bel *ret           = GC_MALLOC(sizeof *ret);
    ret->type          = BEL_STREAM;

    if(!strncmp(name, "ins", 3)) {
        ret->stream.raw_stream = stdin;
    } else if(!strncmp(name, "outs", 4)) {
        ret->stream.raw_stream = stdout;
    } else {
        ret->stream.raw_stream =
            fopen(name,
                  type == BEL_STREAM_READ ? "r" : "w");
    }

    ret->stream.type   = type;
    ret->stream.status = BEL_STREAM_OPEN;
    return ret;
}

Bel*
bel_stream_close(Bel *obj)
{
    // TODO: Check for errors
    if(obj->type != BEL_STREAM
        || obj->stream.status == BEL_STREAM_CLOSED) {
        // TODO: Returning nil here, but should
        // signal an error
        return bel_g_nil;
    }

    if(!fclose(obj->stream.raw_stream)) {
        obj->stream.raw_stream = NULL;
        obj->stream.status     = BEL_STREAM_CLOSED;
        return bel_g_t;
    }

    // TODO: Should signal error on stream closing
    // failure as well
    return bel_g_nil;
}

void
bel_init_streams(void)
{
    bel_g_ins      = bel_g_nil;
    bel_g_outs     = bel_g_nil;
    bel_g_ins_sys  = bel_mkstream("ins",  BEL_STREAM_READ);
    bel_g_outs_sys = bel_mkstream("outs", BEL_STREAM_WRITE);
}

void
bel_init_ax_vars(void)
{
    bel_g_nil   = bel_mksymbol("nil");
    bel_g_t     = bel_mksymbol("t");
    bel_g_o     = bel_mksymbol("o");
    bel_g_apply = bel_mksymbol("apply");
}

char*
bel_conv_bits(uint8_t num)
{
    char *str = GC_MALLOC(9 * sizeof(*str));
    
    uint8_t i;
    for(i = 0; i < 8; i++) {
        int is_bit_set = num & (1 << i);
        str[7 - i] = is_bit_set ? '1' : '0';
    }
    str[8] = '\0';
    
    return str;
}

void
bel_init_ax_chars(void)
{
    // Create a vector of 255 list nodes
    Bel **list = GC_MALLOC(255 * sizeof(*list));

    size_t i;
    for(i = 0; i < 255; i++) {        
        // Build a pair which holds the character information
        Bel *pair = bel_mkpair(bel_mkchar((Bel_char)i),
                               bel_mkstring(bel_conv_bits(i)));
        // Assign the car of a node to the current pair,
        // set its cdr temporarily to nil
        list[i] = bel_mkpair(pair, bel_g_nil);
    }

    // Assign each pair cdr to the pair on the front.
    // Last pair should have a nil cdr still.
    for(i = 0; i < 254; i++) {
        list[i]->pair->cdr = list[i + 1];
    }

    // Hold reference to first element only
    bel_g_chars = list[0];
}

Bel*
bel_env_push(Bel *env, Bel *var, Bel *val)
{
    Bel *new_pair = bel_mkpair(var, val);
    return bel_mkpair(new_pair, env);
}

#define BEL_ENV_GLOBAL_PUSH(SYMSTR, VAL)           \
    (bel_g_globe =                                 \
     bel_env_push(bel_g_globe,                     \
                  bel_mksymbol(SYMSTR), VAL))

void
bel_init_ax_env(void)
{
    BEL_ENV_GLOBAL_PUSH("chars", bel_g_chars);
    BEL_ENV_GLOBAL_PUSH("ins",   bel_g_ins);
    BEL_ENV_GLOBAL_PUSH("outs",  bel_g_outs);
}

Bel*
bel_env_lookup(Bel *env, Bel *sym)
{
    // TODO: Test arguments
    Bel *itr = env;
    while(!bel_nilp(itr)) {
        Bel *p = bel_car(itr);
        if(bel_car(p)->type == BEL_SYMBOL
           && bel_car(p)->sym == sym->sym) {
            return bel_cdr(p);
        }
        
        itr = bel_cdr(itr);
    }
    return bel_g_nil;
}

Bel*
bel_init(void)
{
    GC_INIT();
    Bel_sym_table_init();
    Bel *globe  = GC_MALLOC(sizeof (*globe));
    globe->type = BEL_PAIR;

    // Axioms
    bel_init_ax_vars();
    bel_init_ax_chars();
    bel_init_streams();
    bel_init_ax_env();

    // TODO: Return an environment
    return bel_g_nil;
}

void bel_dbg_print(Bel*); // Forward declaration

void
bel_dbg_print_pair(Bel *obj)
{
    if(bel_nilp(obj)) return;
    
    Bel *itr = obj;
    
    putchar('(');
    while(!bel_nilp(itr)) {
        Bel *car = bel_car(itr);
        Bel *cdr = bel_cdr(itr);

        bel_dbg_print(car);
        
        if(bel_nilp(cdr)) {
            break;
        } else if(cdr->type != BEL_PAIR) {
            putchar(' ');
            putchar('.');
            putchar(' ');
            bel_dbg_print(cdr);
            break;
        }
        putchar(' ');
        itr = cdr;
    }
    putchar(')');
}

void
bel_dbg_print(Bel *obj)
{
    switch(obj->type) {
    case BEL_SYMBOL:
        printf("%s", g_sym_table.tbl[obj->sym]);
        break;
    case BEL_PAIR:   bel_dbg_print_pair(obj);    break;
    case BEL_CHAR:
        if(obj->chr == '\a')
            printf("\\bel"); // There is no Bel without \bel
        else printf("\\%c", obj->chr);
        break;
    case BEL_STREAM: printf("<stream>");         break;
    default:         printf("???");              break; // wat
    };
}

void
string_test()
{
    // There is no Bel without \bel
    Bel *bel  = bel_mkstring("Hello, Bel\a");
    bel_dbg_print(bel);

    printf(" => %s\n", bel_cstring(bel));
}

void
notation_test()
{
    Bel*
    bel = bel_mkpair(bel_mkpair(bel_mksymbol("foo"),
                                bel_mksymbol("bar")),
                     bel_mkpair(bel_mksymbol("baz"),
                                bel_mksymbol("quux")));
    bel_dbg_print(bel);
    putchar(10);
}

void
list_test()
{
    Bel*
    bel = bel_mkpair(
        bel_mksymbol("The"),
        bel_mkpair(
            bel_mksymbol("quick"),
            bel_mkpair(
                bel_mksymbol("brown"),
                bel_mkpair(
                    bel_mksymbol("fox"),
                    bel_mkpair(
                        bel_mksymbol("jumps"),
                        bel_mkpair(
                            bel_mksymbol("over"),
                            bel_mkpair(
                                bel_mksymbol("the"),
                                bel_mkpair(
                                    bel_mksymbol("lazy"),
                                    bel_mkpair(
                                        bel_mksymbol("dog"),
                                        bel_g_nil)))))))));
    bel_dbg_print(bel);
    putchar(10);
}

void
closure_repr_test()
{
    Bel*
    bel = bel_mkpair(bel_mksymbol("lit"),
                     bel_mkpair(
                         bel_mksymbol("clo"),
                         bel_mkpair(
                             bel_g_nil,
                             bel_mkpair(
                                 bel_mkpair(bel_mksymbol("x"),
                                            bel_g_nil),
                                 bel_mkpair(
                                     bel_mkpair(
                                         bel_mksymbol("*"),
                                         bel_mkpair(
                                             bel_mksymbol("x"),
                                             bel_mkpair(
                                                 bel_mksymbol("x"),
                                                 bel_g_nil))),
                                     bel_g_nil)))));
    bel_dbg_print(bel);
    putchar(10);
}

void
character_list_test()
{
    // Character list
    // Char: 0000 => (\0 \0 \0 \0 \0 \0 \0 \0)
    // Char: 0001 => (\0 \0 \0 \0 \0 \0 \0 \1)
    // etc
    Bel *bel = bel_env_lookup(bel_g_globe, bel_mksymbol("chars"));
    int i = 0;
    while(!bel_nilp(bel) && i < 10) {
        Bel *car = bel_car(bel);
        printf("Char: %04d => ", bel_car(car)->chr);
        bel_dbg_print(bel_cdr(car));
        putchar(10);
        bel = bel_cdr(bel);
        i++;
    }
}

void
run_tests()
{
    puts("-- Running debug tests");
    puts("  -- String test");
    string_test();
    puts("  -- Notation test");
    notation_test();
    puts("  -- List test");
    list_test();
    puts("  -- Closure representation test");
    closure_repr_test();
    puts("  -- Character List & Lookup test");
    character_list_test();
}

int
main(void)
{
    printf("Believe %s\n", BELIEVE_VERSION);
    printf("A Bel Lisp interpreter\n");
    printf("Copyright (c) %s\n", BELIEVE_COPYRIGHT);
    printf("This software is distributed under the %s license.\n",
          BELIEVE_LICENSE);

    bel_init();

    run_tests();
    
    return 0;
}
