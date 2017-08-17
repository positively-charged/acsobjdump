/*

   Displays the contents of an ACS object file. The following formats are
   supported: ACS0; ACSE and ACSe, both the direct and indirect formats.

   --------------------------------------------------------------------------

   Below is an outline of the formats of an ACS object file. The ellipsis (...)
   indicates that the sections might not be directly connected; that is, other
   data can appear between the sections.

   Sections of an ACS0 object file:
     <header>
       ...  // ACC and BCC put script code and string content here.
     <script-directory>
     <string-directory>
       ...  // This area here can potentially contain script code and string
            // content, so we need to take this area into consideration.

   Sections of an ACSE/ACSe object file:
     <header>
       ...  // ACC and BCC put the script and function code here.
     <chunk-section>  // ACC and BCC put the chunks here.
       ...  // This area here can potentially contain script and function code
            // or some other data, so we need to take this area into
            // consideration.

   An indirect ACSE/ACSe object file disguises itself as an ACS0 file and hides
   the real header somewhere in the object file. Sections of an indirect
   ACSE/ACSe object file:
     <header>
       ...  // ACC and BCC put the script code here.
     <chunk-section>  // ACC and BCC put the chunks here. This section does not
                      // necessarily need to appear here. Nothing is stopping
                      // the compiler from putting the chunk section at the end
                      // of the object file.
       ...  // ACC and BCC do not put anything here; that is, the chunk section
            // is followed by the real header.
     <real-header>  // The real header is reversed: the chunk-section offset
                    // is the first field, followed by the format name; that
                    // is, in the object file, the chuck-section offset field
                    // has a lower address than the format name field. The real
                    // header is read starting at the back: the format name is
                    // read first, followed by the chunk-section offset.
     <script-directory>  // The directory sections are not used and are only
     <string-directory>  // there to satisfy old wad-editing tools.
       ...  // This area here can potentially contain script and function code
            // or some other data, so we need to take this area into
            // consideration.

*/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <setjmp.h>

#define STATIC_ASSERT( ... ) \
  STATIC_ASSERT_IMPL( __VA_ARGS__,, )
#define STATIC_ASSERT_IMPL( cond, msg, ... ) \
   extern int STATIC_ASSERT__##msg[ !! ( cond ) ]

// Make sure the data types are of sizes we want.
STATIC_ASSERT( CHAR_BIT == 8, CHAR_BIT_must_be_8 );
STATIC_ASSERT( sizeof( char ) == 1, char_must_be_1_byte );
STATIC_ASSERT( sizeof( short ) == 2, short_must_be_2_bytes );
STATIC_ASSERT( sizeof( int ) == 4, int_must_be_4_bytes );
STATIC_ASSERT( sizeof( long long ) == 8, long_long_must_be_8_bytes );

#define DIAG_NONE 0x0
#define DIAG_ERR 0x1
#define DIAG_WARN 0x2
#define DIAG_NOTE 0x4
#define DIAG_INTERNAL 0x80

enum {
   PCD_NOP,
   PCD_TERMINATE,
   PCD_SUSPEND,
   PCD_PUSHNUMBER,
   PCD_LSPEC1,
   PCD_LSPEC2,
   PCD_LSPEC3,
   PCD_LSPEC4,
   PCD_LSPEC5,
   PCD_LSPEC1DIRECT,
   PCD_LSPEC2DIRECT,
   PCD_LSPEC3DIRECT,
   PCD_LSPEC4DIRECT,
   PCD_LSPEC5DIRECT,
   PCD_ADD,
   PCD_SUBTRACT,
   PCD_MULIPLY,
   PCD_DIVIDE,
   PCD_MODULUS,
   PCD_EQ,
   PCD_NE,
   PCD_LT,
   PCD_GT,
   PCD_LE,
   PCD_GE,
   PCD_ASSIGNSCRIPTVAR,
   PCD_ASSIGNMAPVAR,
   PCD_ASSIGNWORLDVAR,
   PCD_PUSHSCRIPTVAR,
   PCD_PUSHMAPVAR,
   PCD_PUSHWORLDVAR,
   PCD_ADDSCRIPTVAR,
   PCD_ADDMAPVAR,
   PCD_ADDWORLDVAR,
   PCD_SUBSCRIPTVAR,
   PCD_SUBMAPVAR,
   PCD_SUBWORLDVAR,
   PCD_MULSCRIPTVAR,
   PCD_MULMAPVAR,
   PCD_MULWORLDVAR,
   PCD_DIVSCRIPTVAR,
   PCD_DIVMAPVAR,
   PCD_DIVWORLDVAR,
   PCD_MODSCRIPTVAR,
   PCD_MODMAPVAR,
   PCD_MODWORLDVAR,
   PCD_INCSCRIPTVAR,
   PCD_INCMAPVAR,
   PCD_INCWORLDVAR,
   PCD_DECSCRIPTVAR,
   PCD_DECMAPVAR,
   PCD_DECWORLDVAR,
   PCD_GOTO,
   PCD_IFGOTO,
   PCD_DROP,
   PCD_DELAY,
   PCD_DELAYDIRECT,
   PCD_RANDOM,
   PCD_RANDOMDIRECT,
   PCD_THINGCOUNT,
   PCD_THINGCOUNTDIRECT,
   PCD_TAGWAIT,
   PCD_TAGWAITDIRECT,
   PCD_POLYWAIT,
   PCD_POLYWAITDIRECT,
   PCD_CHANGEFLOOR,
   PCD_CHANGEFLOORDIRECT,
   PCD_CHANGECEILING,
   PCD_CHANGECEILINGDIRECT,
   PCD_RESTART,
   PCD_ANDLOGICAL,
   PCD_ORLOGICAL,
   PCD_ANDBITWISE,
   PCD_ORBITWISE,
   PCD_EORBITWISE,
   PCD_NEGATELOGICAL,
   PCD_LSHIFT,
   PCD_RSHIFT,
   PCD_UNARYMINUS,
   PCD_IFNOTGOTO,
   PCD_LINESIDE,
   PCD_SCRIPTWAIT,
   PCD_SCRIPTWAITDIRECT,
   PCD_CLEARLINESPECIAL,
   PCD_CASEGOTO,
   PCD_BEGINPRINT,
   PCD_ENDPRINT,
   PCD_PRINTSTRING,
   PCD_PRINTNUMBER,
   PCD_PRINTCHARACTER,
   PCD_PLAYERCOUNT,
   PCD_GAMETYPE,
   PCD_GAMESKILL,
   PCD_TIMER,
   PCD_SECTORSOUND,
   PCD_AMBIENTSOUND,
   PCD_SOUNDSEQUENCE,
   PCD_SETLINETEXTURE,
   PCD_SETLINEBLOCKING,
   PCD_SETLINESPECIAL,
   PCD_THINGSOUND,
   PCD_ENDPRINTBOLD,
   PCD_ACTIVATORSOUND,
   PCD_LOCALAMBIENTSOUND,
   PCD_SETLINEMONSTERBLOCKING,
   PCD_PLAYERBLUESKULL,
   PCD_PLAYERREDSKULL,
   PCD_PLAYERYELLOWSKULL,
   PCD_PLAYERMASTERSKULL,
   PCD_PLAYERBLUECARD,
   PCD_PLAYERREDCARD,
   PCD_PLAYERYELLOWCARD,
   PCD_PLAYERMASTERCARD,
   PCD_PLAYERBLACKSKULL,
   PCD_PLAYERSILVERSKULL,
   PCD_PLAYERGOLDSKULL,
   PCD_PLAYERBLACKCARD,
   PCD_PLAYERSILVERCARD,
   PCD_ISMULTIPLAYER,
   PCD_PLAYERTEAM,
   PCD_PLAYERHEALTH,
   PCD_PLAYERARMORPOINTS,
   PCD_PLAYERFRAGS,
   PCD_PLAYEREXPERT,
   PCD_BLUETEAMCOUNT,
   PCD_REDTEAMCOUNT,
   PCD_BLUETEAMSCORE,
   PCD_REDTEAMSCORE,
   PCD_ISONEFLAGCTF,
   PCD_GETINVASIONWAVE,
   PCD_GETINVASIONSTATE,
   PCD_PRINTNAME,
   PCD_MUSICCHANGE,
   PCD_CONSOLECOMMANDDIRECT,
   PCD_CONSOLECOMMAND,
   PCD_SINGLEPLAYER,
   PCD_FIXEDMUL,
   PCD_FIXEDDIV,
   PCD_SETGRAVITY,
   PCD_SETGRAVITYDIRECT,
   PCD_SETAIRCONTROL,
   PCD_SETAIRCONTROLDIRECT,
   PCD_CLEARINVENTORY,
   PCD_GIVEINVENTORY,
   PCD_GIVEINVENTORYDIRECT,
   PCD_TAKEINVENTORY,
   PCD_TAKEINVENTORYDIRECT,
   PCD_CHECKINVENTORY,
   PCD_CHECKINVENTORYDIRECT,
   PCD_SPAWN,
   PCD_SPAWNDIRECT,
   PCD_SPAWNSPOT,
   PCD_SPAWNSPOTDIRECT,
   PCD_SETMUSIC,
   PCD_SETMUSICDIRECT,
   PCD_LOCALSETMUSIC,
   PCD_LOCALSETMUSICDIRECT,
   PCD_PRINTFIXED,
   PCD_PRINTLOCALIZED,
   PCD_MOREHUDMESSAGE,
   PCD_OPTHUDMESSAGE,
   PCD_ENDHUDMESSAGE,
   PCD_ENDHUDMESSAGEBOLD,
   PCD_SETSTYLE,
   PCD_SETSTYLEDIRECT,
   PCD_SETFONT,
   PCD_SETFONTDIRECT,
   PCD_PUSHBYTE,
   PCD_LSPEC1DIRECTB,
   PCD_LSPEC2DIRECTB,
   PCD_LSPEC3DIRECTB,
   PCD_LSPEC4DIRECTB,
   PCD_LSPEC5DIRECTB,
   PCD_DELAYDIRECTB,
   PCD_RANDOMDIRECTB,
   PCD_PUSHBYTES,
   PCD_PUSH2BYTES,
   PCD_PUSH3BYTES,
   PCD_PUSH4BYTES,
   PCD_PUSH5BYTES,
   PCD_SETTHINGSPECIAL,
   PCD_ASSIGNGLOBALVAR,
   PCD_PUSHGLOBALVAR,
   PCD_ADDGLOBALVAR,
   PCD_SUBGLOBALVAR,
   PCD_MULGLOBALVAR,
   PCD_DIVGLOBALVAR,
   PCD_MODGLOBALVAR,
   PCD_INCGLOBALVAR,
   PCD_DECGLOBALVAR,
   PCD_FADETO,
   PCD_FADERANGE,
   PCD_CANCELFADE,
   PCD_PLAYMOVIE,
   PCD_SETFLOORTRIGGER,
   PCD_SETCEILINGTRIGGER,
   PCD_GETACTORX,
   PCD_GETACTORY,
   PCD_GETACTORZ,
   PCD_STARTTRANSLATION,
   PCD_TRANSLATIONRANGE1,
   PCD_TRANSLATIONRANGE2,
   PCD_ENDTRANSLATION,
   PCD_CALL,
   PCD_CALLDISCARD,
   PCD_RETURNVOID,
   PCD_RETURNVAL,
   PCD_PUSHMAPARRAY,
   PCD_ASSIGNMAPARRAY,
   PCD_ADDMAPARRAY,
   PCD_SUBMAPARRAY,
   PCD_MULMAPARRAY,
   PCD_DIVMAPARRAY,
   PCD_MODMAPARRAY,
   PCD_INCMAPARRAY,
   PCD_DECMAPARRAY,
   PCD_DUP,
   PCD_SWAP,
   PCD_WRITETOINI,
   PCD_GETFROMINI,
   PCD_SIN,
   PCD_COS,
   PCD_VECTORANGLE,
   PCD_CHECKWEAPON,
   PCD_SETWEAPON,
   PCD_TAGSTRING,
   PCD_PUSHWORLDARRAY,
   PCD_ASSIGNWORLDARRAY,
   PCD_ADDWORLDARRAY,
   PCD_SUBWORLDARRAY,
   PCD_MULWORLDARRAY,
   PCD_DIVWORLDARRAY,
   PCD_MODWORLDARRAY,
   PCD_INCWORLDARRAY,
   PCD_DECWORLDARRAY,
   PCD_PUSHGLOBALARRAY,
   PCD_ASSIGNGLOBALARRAY,
   PCD_ADDGLOBALARRAY,
   PCD_SUBGLOBALARRAY,
   PCD_MULGLOBALARRAY,
   PCD_DIVGLOBALARRAY,
   PCD_MODGLOBALARRAY,
   PCD_INCGLOBALARRAY,
   PCD_DECGLOBALARRAY,
   PCD_SETMARINEWEAPON,
   PCD_SETACTORPROPERTY,
   PCD_GETACTORPROPERTY,
   PCD_PLAYERNUMBER,
   PCD_ACTIVATORTID,
   PCD_SETMARINESPRITE,
   PCD_GETSCREENWIDTH,
   PCD_GETSCREENHEIGHT,
   PCD_THINGPROJECTILE2,
   PCD_STRLEN,
   PCD_SETHUDSIZE,
   PCD_GETCVAR,
   PCD_CASEGOTOSORTED,
   PCD_SETRESULTVALUE,
   PCD_GETLINEROWOFFSET,
   PCD_GETACTORFLOORZ,
   PCD_GETACTORANGLE,
   PCD_GETSECTORFLOORZ,
   PCD_GETSECTORCEILINGZ,
   PCD_LSPEC5RESULT,
   PCD_GETSIGILPIECES,
   PCD_GETLEVELINFO,
   PCD_CHANGESKY,
   PCD_PLAYERINGAME,
   PCD_PLAYERISBOT,
   PCD_SETCAMERATOTEXTURE,
   PCD_ENDLOG,
   PCD_GETAMMOCAPACITY,
   PCD_SETAMMOCAPACITY,
   PCD_PRINTMAPCHARARRAY,
   PCD_PRINTWORLDCHARARRAY,
   PCD_PRINTGLOBALCHARARRAY,
   PCD_SETACTORANGLE,
   PCD_GRAPINPUT,
   PCD_SETMOUSEPOINTER,
   PCD_MOVEMOUSEPOINTER,
   PCD_SPAWNPROJECTILE,
   PCD_GETSECTORLIGHTLEVEL,
   PCD_GETACTORCEILINGZ,
   PCD_SETACTORPOSITION,
   PCD_CLEARACTORINVENTORY,
   PCD_GIVEACTORINVENTORY,
   PCD_TAKEACTORINVENTORY,
   PCD_CHECKACTORINVENTORY,
   PCD_THINGCOUNTNAME,
   PCD_SPAWNSPOTFACING,
   PCD_PLAYERCLASS,
   PCD_ANDSCRIPTVAR,
   PCD_ANDMAPVAR,
   PCD_ANDWORLDVAR,
   PCD_ANDGLOBALVAR,
   PCD_ANDMAPARRAY,
   PCD_ANDWORLDARRAY,
   PCD_ANDGLOBALARRAY,
   PCD_EORSCRIPTVAR,
   PCD_EORMAPVAR,
   PCD_EORWORLDVAR,
   PCD_EORGLOBALVAR,
   PCD_EORMAPARRAY,
   PCD_EORWORLDARRAY,
   PCD_EORGLOBALARRAY,
   PCD_ORSCRIPTVAR,
   PCD_ORMAPVAR,
   PCD_ORWORLDVAR,
   PCD_ORGLOBALVAR,
   PCD_ORMAPARRAY,
   PCD_ORWORLDARRAY,
   PCD_ORGLOBALARRAY,
   PCD_LSSCRIPTVAR,
   PCD_LSMAPVAR,
   PCD_LSWORLDVAR,
   PCD_LSGLOBALVAR,
   PCD_LSMAPARRAY,
   PCD_LSWORLDARRAY,
   PCD_LSGLOBALARRAY,
   PCD_RSSCRIPTVAR,
   PCD_RSMAPVAR,
   PCD_RSWORLDVAR,
   PCD_RSGLOBALVAR,
   PCD_RSMAPARRAY,
   PCD_RSWORLDARRAY,
   PCD_RSGLOBALARRAY,
   PCD_GETPLAYERINFO,
   PCD_CHANGELEVEL,
   PCD_SECTORDAMAGE,
   PCD_REPLACETEXTURES,
   PCD_NEGATEBINARY,
   PCD_GETACTORPITCH,
   PCD_SETACTORPITCH,
   PCD_PRINTBIND,
   PCD_SETACTORSTATE,
   PCD_THINGDAMAGE2,
   PCD_USEINVENTORY,
   PCD_USEACTORINVENTORY,
   PCD_CHECKACTORCEILINGTEXTURE,
   PCD_CHECKACTORFLOORTEXTURE,
   PCD_GETACTORLIGHTLEVEL,
   PCD_SETMUGSHOTSTATE,
   PCD_THINGCOUNTSECTOR,
   PCD_THINGCOUNTNAMESECTOR,
   PCD_CHECKPLAYERCAMERA,
   PCD_MORPHACTOR,
   PCD_UNMORPHACTOR,
   PCD_GETPLAYERINPUT,
   PCD_CLASSIFYACTOR,
   PCD_PRINTBINARY,
   PCD_PRINTHEX,
   PCD_CALLFUNC,
   PCD_SAVESTRING,
   PCD_PRINTMAPCHRANGE,
   PCD_PRINTWORLDCHRANGE,
   PCD_PRINTGLOBALCHRANGE,
   PCD_STRCPYTOMAPCHRANGE,
   PCD_STRCPYTOWORLDCHRANGE,
   PCD_STRCPYTOGLOBALCHRANGE,
   PCD_PUSHFUNCTION,
   PCD_CALLSTACK,
   PCD_SCRIPTWAITNAMED,
   PCD_TRANSLATIONRANGE3,
   PCD_GOTOSTACK,
   PCD_ASSIGNSCRIPTARRAY,
   PCD_PUSHSCRIPTARRAY,
   PCD_ADDSCRIPTARRAY,
   PCD_SUBSCRIPTARRAY,
   PCD_MULSCRIPTARRAY,
   PCD_DIVSCRIPTARRAY,
   PCD_MODSCRIPTARRAY,
   PCD_INCSCRIPTARRAY,
   PCD_DECSCRIPTARRAY,
   PCD_ANDSCRIPTARRAY,
   PCD_EORSCRIPTARRAY,
   PCD_ORSCRIPTARRAY,
   PCD_LSSCRIPTARRAY,
   PCD_RSSCRIPTARRAY,
   PCD_PRINTSCRIPTCHARARRAY,
   PCD_PRINTSCRIPTCHRANGE,
   PCD_STRCPYTOSCRIPTCHRANGE,
   PCD_LSPEC5EX,
   PCD_LSPEC5EXRESULT,
   PCD_TRANSLATIONRANGE4,
   PCD_TRANSLATIONRANGE5,
   PCD_TOTAL
};

struct options {
   const char* file;
   const char* view_chunk;
   bool list_chunks;
};

struct object {
   const unsigned char* data;
   int size;
   enum {
      FORMAT_UNKNOWN,
      FORMAT_ZERO,
      FORMAT_BIG_E,
      FORMAT_LITTLE_E,
   } format;
   int directory_offset;
   int string_offset;
   int real_header_offset;
   int chunk_offset;
   bool indirect_format;
   bool small_code;
};

struct header {
   char id[ 4 ];
   int offset;
};

struct chunk {
   char name[ 5 ];
   const unsigned char* data; 
   int size;
   enum {
      CHUNK_UNKNOWN,
      CHUNK_ARAY,
      CHUNK_AINI,
      CHUNK_AIMP,
      CHUNK_ASTR,
      CHUNK_MSTR,
      CHUNK_ATAG,
      CHUNK_LOAD,
      CHUNK_FUNC,
      CHUNK_FNAM,
      CHUNK_MINI,
      CHUNK_MIMP,
      CHUNK_MEXP,
      CHUNK_SPTR,
      CHUNK_SFLG,
      CHUNK_SVCT,
      CHUNK_SNAM,
      CHUNK_STRL,
      CHUNK_STRE,
      CHUNK_SARY,
      CHUNK_FARY,
      CHUNK_ALIB,
   } type;
};

struct chunk_header {
   char name[ 4 ];
   int size;
};

struct chunk_reader {
   struct object* object;
   int end_pos;
   int pos;
};

struct common_acse_script_entry {
   int number;
   int type;
   int num_param;
   int offset;
   int real_entry_size;
};

struct acs0_script_entry {
   int number;
   int offset;
   int num_param;
};

struct func_entry {
   unsigned char num_param;
   unsigned char size;
   unsigned char has_return;
   unsigned char padding;
   int offset;
};

struct pcode_segment {
   const unsigned char* data_start;
   const unsigned char* data;
   int offset;
   int code_size;
   int opcode;
   bool invalid_opcode;
};

struct viewer {
   struct options* options;
   unsigned char* object_data;
   int object_size;
   jmp_buf bail;
}; 

static void init_options( struct options* options );
static bool read_options( struct options* options, int argc, char** argv );
static void option_err( const char* format, ... );
static bool run( struct options* options );
static void init_viewer( struct viewer* viewer, struct options* options );
static void deinit_viewer( struct viewer* viewer );
static void read_object_file( struct viewer* viewer );
static bool read_object_file_data( struct viewer* viewer, FILE* fh );
static bool perform_operation( struct viewer* viewer );
static void init_object( struct object* object, const unsigned char* data,
   int size );
static int data_left( struct object* object, const unsigned char* data );
static bool offset_in_range( struct object* object, const unsigned char* start,
   const unsigned char* end, int offset );
static bool offset_in_object_file( struct object* object, int offset );
static void expect_data( struct viewer* viewer, struct object* object,
   const unsigned char* start, int size );
static void expect_offset_in_object_file( struct viewer* viewer,
   struct object* object, int offset );
static void expect_chunk_offset_in_chunk( struct viewer* viewer,
   struct chunk* chunk, int offset );
static void expect_chunk_data( struct viewer* viewer, struct chunk* chunk,
   const unsigned char* start, int size );
static int chunk_data_left( struct chunk* chunk, const unsigned char* data );
static bool chunk_offset_in_range( struct chunk* chunk,
   const unsigned char* start, const unsigned char* end, int offset );
static bool chunk_offset_in_chunk( struct chunk* chunk, int offset );
static void determine_format( struct viewer* viewer, struct object* object );
static bool peek_real_id( struct object* object, struct header* header );
static void determine_object_offsets( struct viewer* viewer,
   struct object* object );
static bool script_directory_present( struct object* object );
static void list_chunks( struct viewer* viewer, struct object* object );
static bool show_chunk( struct viewer* viewer, struct object* object,
   struct chunk* chunk, bool show_contents );
static void show_aray( struct viewer* viewer, struct chunk* chunk );
static void show_aini( struct viewer* viewer, struct chunk* chunk );
static void show_aimp( struct viewer* viewer, struct chunk* chunk );
static void show_astr_mstr( struct viewer* viewer, struct chunk* chunk );
static void show_atag( struct viewer* viewer, struct chunk* chunk );
static void show_atag_version0( struct viewer* viewer, struct chunk* chunk );
static void show_load( struct viewer* viewer, struct chunk* chunk );
static void show_func( struct viewer* viewer, struct object* object,
   struct chunk* chunk );
static void show_fnam( struct viewer* viewer, struct chunk* chunk );
static void show_mini( struct viewer* viewer, struct chunk* chunk );
static void show_mimp( struct viewer* viewer, struct chunk* chunk );
static void show_mexp( struct viewer* viewer, struct chunk* chunk );
static void show_sptr( struct viewer* viewer, struct object* object,
   struct chunk* chunk );
static void read_acse_script_entry( struct viewer* viewer,
   struct object* object, struct chunk* chunk, const unsigned char* data,
   struct common_acse_script_entry* common_entry );
static int calc_code_size( struct viewer* viewer, struct object* object,
   int offset );
static const char* get_script_type_name( int type );
static void show_pcode( struct viewer* viewer, struct object* object,
   int offset, int code_size );
static void init_pcode_segment( struct object* object,
   struct pcode_segment* segment, int offset, int code_size );
static bool pcode_segment_end( struct pcode_segment* segment );
static void expect_pcode_data( struct viewer* viewer,
   struct pcode_segment* segment, int size );
static void show_instruction( struct viewer* viewer, struct object* object,
   struct pcode_segment* segment );
static void show_opcode( struct viewer* viewer, struct object* object,
   struct pcode_segment* segment );
static void show_args( struct viewer* viewer, struct object* object,
   struct pcode_segment* segment );
static void show_sflg( struct viewer* viewer, struct chunk* chunk );
static void show_svct( struct viewer* viewer, struct chunk* chunk );
static void show_snam( struct viewer* viewer, struct chunk* chunk );
static const char* read_chunk_string( struct viewer* viewer,
   struct chunk* chunk, int offset );
static void show_strl_stre( struct viewer* viewer, struct chunk* chunk );
static const char* read_strl_stre_string( struct viewer* viewer,
   struct chunk* chunk, int offset );
static bool is_strl_stre_string_nul_terminated( struct chunk* chunk,
   int offset );
static char decode_ch( int string_offset, int offset, char ch );
static void show_string( int index, int offset, const char* value,
   bool is_encoded );
static void show_sary_fary( struct viewer* viewer, struct chunk* chunk );
static void show_alib( struct chunk* chunk );
static bool view_chunk( struct viewer* viewer, struct object* object,
   const char* name );
static void init_chunk_reader( struct chunk_reader* reader,
   struct object* object );
static bool read_chunk( struct viewer* viewer, struct chunk_reader* reader,
   struct chunk* chunk );
static void init_chunk( struct viewer* viewer, struct object* object,
   int offset, struct chunk* chunk );
static int get_chunk_type( const char* name );
static bool find_chunk( struct viewer* viewer, struct object* object,
   const char* name, struct chunk* chunk );
static void show_object( struct viewer* viewer, struct object* object );
static void show_all_chunks( struct viewer* viewer, struct object* object );
static void show_script_directory( struct viewer* viewer,
   struct object* object );
static void show_string_directory( struct viewer* viewer,
   struct object* object );
static void diag( struct viewer* viewer, int flags, const char* format, ... );
static void bail( struct viewer* viewer );

static struct {
   const char* name;
   int num_args;
} g_pcodes[] = {
   { "nop", 0 },
   { "terminate", 0 },
   { "suspend", 0 },
   { "pushnumber", 1 },
   { "lspec1", 1 },
   { "lspec2", 1 },
   { "lspec3", 1 },
   { "lspec4", 1 },
   { "lspec5", 1 },
   { "lspec1direct", 2 },
   { "lspec2direct", 3 },
   { "lspec3direct", 4 },
   { "lspec4direct", 5 },
   { "lspec5direct", 6 },
   { "add", 0 },
   { "subtract", 0 },
   { "multiply", 0 },
   { "divide", 0 },
   { "modulus", 0 },
   { "eq", 0 },
   { "ne", 0 },
   { "lt", 0 },
   { "gt", 0 },
   { "le", 0 },
   { "ge", 0 },
   { "assignscriptvar", 1 },
   { "assignmapvar", 1 },
   { "assignworldvar", 1 },
   { "pushscriptvar", 1 },
   { "pushmapvar", 1 },
   { "pushworldvar", 1 },
   { "addscriptvar", 1 },
   { "addmapvar", 1 },
   { "addworldvar", 1 },
   { "subscriptvar", 1 },
   { "submapvar", 1 },
   { "subworldvar", 1 },
   { "mulscriptvar", 1 },
   { "mulmapvar", 1 },
   { "mulworldvar", 1 },
   { "divscriptvar", 1 },
   { "divmapvar", 1 },
   { "divworldvar", 1 },
   { "modscriptvar", 1 },
   { "modmapvar", 1 },
   { "modworldvar", 1 },
   { "incscriptvar", 1 },
   { "incmapvar", 1 },
   { "incworldvar", 1 },
   { "decscriptvar", 1 },
   { "decmapvar", 1 },
   { "decworldvar", 1 },
   { "goto", 1 },
   { "ifgoto", 1 },
   { "drop", 0 },
   { "delay", 0 },
   { "delaydirect", 1 },
   { "random", 0 },
   { "randomdirect", 2 },
   { "thingcount", 0 },
   { "thingcountdirect", 2 },
   { "tagwait", 0 },
   { "tagwaitdirect", 1 },
   { "polywait", 0 },
   { "polywaitdirect", 1 },
   { "changefloor", 0 },
   { "changefloordirect", 2 },
   { "changeceiling", 0 },
   { "changeceilingdirect", 2 },
   { "restart", 0 },
   { "andlogical", 0 },
   { "orlogical", 0 },
   { "andbitwise", 0 },
   { "orbitwise", 0 },
   { "eorbitwise", 0 },
   { "negatelogical", 0 },
   { "lshift", 0 },
   { "rshift", 0 },
   { "unaryminus", 0 },
   { "ifnotgoto", 1 },
   { "lineside", 0 },
   { "scriptwait", 0 },
   { "scriptwaitdirect", 1 },
   { "clearlinespecial", 0 },
   { "casegoto", 2 },
   { "beginprint", 0 },
   { "endprint", 0 },
   { "printstring", 0 },
   { "printnumber", 0 },
   { "printcharacter", 0 },
   { "playercount", 0 },
   { "gametype", 0 },
   { "gameskill", 0 },
   { "timer", 0 },
   { "sectorsound", 0 },
   { "ambientsound", 0 },
   { "soundsequence", 0 },
   { "setlinetexture", 0 },
   { "setlineblocking", 0 },
   { "setlinespecial", 0 },
   { "thingsound", 0 },
   { "endprintbold", 0 },
   { "activatorsound", 0 },
   { "ambientsound", 0 },
   { "setlinemonsterblocking", 0 },
   { "playerblueskull", 0 },
   { "playerredskull", 0 },
   { "playeryellowskull", 0 },
   { "playermasterskull", 0 },
   { "playerbluecard", 0 },
   { "playerredcard", 0 },
   { "playeryellowcard", 0 },
   { "playermastercard", 0 },
   { "playerblackskull", 0 },
   { "playersilverskull", 0 },
   { "playergoldskull", 0 },
   { "playerblackcard", 0 },
   { "playersilvercard", 0 },
   { "ismultiplayer", 0 },
   { "playerteam", 0 },
   { "playerhealth", 0 },
   { "playerarmorpoints", 0 },
   { "playerfrags", 0 },
   { "playerexpert", 0 },
   { "blueteamcount", 0 },
   { "redteamcount", 0 },
   { "blueteamscore", 0 },
   { "redteamscore", 0 },
   { "isoneflagctf", 0 },
   { "getinvasionwave", 0 },
   { "getinvastionstate", 0 },
   { "printname", 0 },
   { "musicchange", 0 },
   { "consolecommanddirect", 3 },
   { "consolecommand", 0 },
   { "singleplayer", 0 },
   { "fixedmul", 0 },
   { "fixeddiv", 0 },
   { "setgravity", 0 },
   { "setgravitydirect", 1 },
   { "setaircontrol", 0 },
   { "setaircontroldirect", 1 },
   { "clearinventory", 0 },
   { "giveinventory", 0 },
   { "giveinventorydirect", 2 },
   { "takeinventory", 0 },
   { "takeinventorydirect", 2 },
   { "checkinventory", 0 },
   { "checkinventorydirect", 1 },
   { "spawn", 0 },
   { "spawndirect", 6 },
   { "spawnspot", 0 },
   { "spawnspotdirect", 4 },
   { "setmusic", 0 },
   { "setmusicdirect", 3 },
   { "localsetmusic", 0 },
   { "localsetmusicdirect", 3 },
   { "printfixed", 0 },
   { "printlocalized", 0 },
   { "morehudmessage", 0 },
   { "opthudmessage", 0 },
   { "endhudmessage", 0 },
   { "endhudmessagebold", 0 },
   { "setstyle", 0 },
   { "setstyledirect", 0 },
   { "setfont", 0 },
   { "setfontdirect", 1 },
   { "pushbyte", 1 },
   { "lspec1directb", 2 },
   { "lspec2directb", 3 },
   { "lspec3directb", 4 },
   { "lspec4directb", 5 },
   { "lspec5directb", 6 },
   { "delaydirectb", 1 },
   { "randomdirectb", 2 },
   { "pushbytes", -1 },
   { "push2bytes", 2 },
   { "push3bytes", 3 },
   { "push4bytes", 4 },
   { "push5bytes", 5 },
   { "setthingspecial", 0 },
   { "assignglobalvar", 1 },
   { "pushglobalvar", 1 },
   { "addglobalvar", 1 },
   { "subglobalvar", 1 },
   { "mulglobalvar", 1 },
   { "divglobalvar", 1 },
   { "modglobalvar", 1 },
   { "incglobalvar", 1 },
   { "decglobalvar", 1 },
   { "fadeto", 0 },
   { "faderange", 0 },
   { "cancelfade", 0 },
   { "playmovie", 0 },
   { "setfloortrigger", 0 },
   { "setceilingtrigger", 0 },
   { "getactorx", 0 },
   { "getactory", 0 },
   { "getactorz", 0 },
   { "starttranslation", 0 },
   { "translationrange1", 0 },
   { "translationrange2", 0 },
   { "endtranslation", 0 },
   { "call", 1 },
   { "calldiscard", 1 },
   { "returnvoid", 0 },
   { "returnval", 0 },
   { "pushmaparray", 1 },
   { "assignmaparray", 1 },
   { "addmaparray", 1 },
   { "submaparray", 1 },
   { "mulmaparray", 1 },
   { "divmaparray", 1 },
   { "modmaparray", 1 },
   { "incmaparray", 1 },
   { "decmaparray", 1 },
   { "dup", 0 },
   { "swap", 0 },
   { "writetoini", 0 },
   { "getfromini", 0 },
   { "sin", 0 },
   { "cos", 0 },
   { "vectorangle", 0 },
   { "checkweapon", 0 },
   { "setweapon", 0 },
   { "tagstring", 0 },
   { "pushworldarray", 1 },
   { "assignworldarray", 1 },
   { "addworldarray", 1 },
   { "subworldarray", 1 },
   { "mulworldarray", 1 },
   { "divworldarray", 1 },
   { "modworldarray", 1 },
   { "incworldarray", 1 },
   { "decworldarray", 1 },
   { "pushglobalarray", 1 },
   { "assignglobalarray", 1 },
   { "addglobalarray", 1 },
   { "subglobalarray", 1 },
   { "mulglobalarray", 1 },
   { "divglobalarray", 1 },
   { "modglobalarray", 1 },
   { "incglobalarray", 1 },
   { "decglobalarray", 1 },
   { "setmarineweapon", 0 },
   { "setactorproperty", 0 },
   { "getactorproperty", 0 },
   { "playernumber", 0 },
   { "activatortid", 0 },
   { "setmarinesprite", 0 },
   { "getscreenwidth", 0 },
   { "getscreenheight", 0 },
   { "thingprojectile2", 0 },
   { "strlen", 0 },
   { "gethudsize", 0 },
   { "getcvar", 0 },
   { "casegotosorted", -1 },
   { "setresultvalue", 0 },
   { "getlinerowoffset", 0 },
   { "getactorfloorz", 0 },
   { "getactorangle", 0 },
   { "getsectorfloorz", 0 },
   { "getsectorceilingz", 0 },
   { "lspec5result", 1 },
   { "getsigilpieces", 0 },
   { "getlevelinfo", 0 },
   { "changesky", 0 },
   { "playeringame", 0 },
   { "playerisbot", 0 },
   { "setcameratotexture", 0 },
   { "endlog", 0 },
   { "getammocapacity", 0 },
   { "setammocapacity", 0 },
   { "printmapchararray", 0 },
   { "printworldchararray", 0 },
   { "printglobalchararray", 0 },
   { "setactorangle", 0 },
   { "grabinput", 0 },
   { "setmousepointer", 0 },
   { "movemousepointer", 0 },
   { "spawnprojectile", 0 },
   { "getsectorlightlevel", 0 },
   { "getactorceilingz", 0 },
   { "setactorposition", 0 },
   { "clearactorinventory", 0 },
   { "giveactorinventory", 0 },
   { "takeactorinventory", 0 },
   { "checkactorinventory", 0 },
   { "thingcountname", 0 },
   { "spawnspotfacing", 0 },
   { "playerclass", 0 },
   { "andscriptvar", 1 },
   { "andmapvar", 1 },
   { "andworldvar", 1 },
   { "andglobalvar", 1 },
   { "andmaparray", 1 },
   { "andworldarray", 1 },
   { "andglobalarray", 1 },
   { "eorscriptvar", 1 },
   { "eormapvar", 1 },
   { "eorworldvar", 1 },
   { "eorglobalvar", 1 },
   { "eormaparray", 1 },
   { "eorworldarray", 1 },
   { "eorglobalarray", 1 },
   { "orscriptvar", 1 },
   { "ormapvar", 1 },
   { "orworldvar", 1 },
   { "orglobalvar", 1 },
   { "ormaparray", 1 },
   { "orworldarray", 1 },
   { "orglobalarray", 1 },
   { "lsscriptvar", 1 },
   { "lsmapvar", 1 },
   { "lsworldvar", 1 },
   { "lsglobalvar", 1 },
   { "lsmaparray", 1 },
   { "lsworldarray", 1 },
   { "lsglobalarray", 1 },
   { "rsscriptvar", 1 },
   { "rsmapvar", 1 },
   { "rsworldvar", 1 },
   { "rsglobalvar", 1 },
   { "rsmaparray", 1 },
   { "rsworldarray", 1 },
   { "rsglobalarray", 1 },
   { "getplayerinfo", 0 },
   { "changelevel", 0 },
   { "sectordamage", 0 },
   { "replacetextures", 0 },
   { "negatebinary", 0 },
   { "getactorpitch", 0 },
   { "setactorpitch", 0 },
   { "printbind", 0 },
   { "setactorstate", 0 },
   { "thingdamage2", 0 },
   { "useinventory", 0 },
   { "useactorinventory", 0 },
   { "checkactorceilingtexture", 0 },
   { "checkactorfloortexture", 0 },
   { "getactorlightlevel", 0 },
   { "setmugshotstate", 0 },
   { "thingcountsector", 0 },
   { "thingcountnamesector", 0 },
   { "checkplayercamera", 0 },
   { "morphactor", 0 },
   { "unmorphactor", 0 },
   { "getplayerinput", 0 },
   { "classifyactor", 0 },
   { "printbinary", 0 },
   { "printhex", 0 },
   { "callfunc", 2 },
   { "savestring", 0 },
   { "printmapchrange", 0 },
   { "printworldchrange", 0 },
   { "printglobalchrange", 0 },
   { "strcpytomapchrange", 0 },
   { "strcpytoworldchrange", 0 },
   { "strcpytoglobalchrange", 0 },
   { "pushfunction", 1 },
   { "callstack", 0 },
   { "scriptwaitnamed", 0 },
   { "translationrange3", 0 },
   { "gotostack", 0 },
   { "assignscriptarray", 1 },
   { "pushscriptarray", 1 },
   { "addscriptarray", 1 },
   { "subscriptarray", 1 },
   { "mulscriptarray", 1 },
   { "divscriptarray", 1 },
   { "modscriptarray", 1 },
   { "incscriptarray", 1 },
   { "decscriptarray", 1 },
   { "andscriptarray", 1 },
   { "eorscriptarray", 1 },
   { "orscriptarray", 1 },
   { "lsscriptarray", 1 },
   { "rsscriptarray", 1 },
   { "printscriptchararray", 0 },
   { "printscriptchrange", 0 },
   { "strcpytoscriptchrange", 0 },
   { "lspec5ex", 1 },
   { "lspec5exresult", 1 },
   { "translationrange4", 0 },
   { "translationrange5", 0 },
};

int main( int argc, char* argv[] ) {
   int result = EXIT_FAILURE;
   struct options options;
   init_options( &options );
   if ( read_options( &options, argc, argv ) ) {
      bool success = run( &options );
      if ( success ) {
         result = EXIT_SUCCESS;
      }
   }
   return result;
}

static void init_options( struct options* options ) {
   options->file = NULL;
   options->view_chunk = NULL;
   options->list_chunks = false;
}

static bool read_options( struct options* options, int argc, char** argv ) {
   if ( argc > 1 ) {
      int i = 1;
      while ( argv[ i ] && argv[ i ][ 0 ] == '-' ) {
         switch ( argv[ i ][ 1 ] ) {
         case 'c':
            if ( argv[ i + 1 ] ) {
               options->view_chunk = argv[ i + 1 ];
               i += 2;
            }
            else {
               option_err( "missing chunk to view" );
               return false;
            }
            break;
         case 'l':
            options->list_chunks = true;
            ++i;
            break;
         default:
            option_err( "unknown option: %c", argv[ i ][ 1 ] );
            return false;
         }
      }
      if ( argv[ i ] ) {
         options->file = argv[ i ];
         return true;
      }
      else {
         option_err( "missing object file" );
         return false;
      }
   }
   else {
      printf(
         "%s [options] <object-file>\n"
         "Options:\n"
         "  -c <chunk>    View selected chunk\n"
         "  -l            List chunks in object file\n",
         argv[ 0 ] );
      return false;
   }
}

static void option_err( const char* format, ... ) {
   printf( "option error: " );
   va_list args;
   va_start( args, format );
   vprintf( format, args );
   va_end( args );
   printf( "\n" );
}

static bool run( struct options* options ) {
   bool success = false;
   struct viewer viewer;
   init_viewer( &viewer, options );
   if ( setjmp( viewer.bail ) == 0 ) {
      read_object_file( &viewer );
      perform_operation( &viewer );
      success = true;
   }
   deinit_viewer( &viewer );
   return success;
}

static void init_viewer( struct viewer* viewer, struct options* options ) {
   viewer->options = options;
   viewer->object_data = NULL;
   viewer->object_size = 0;
}

static void deinit_viewer( struct viewer* viewer ) {
   if ( viewer->object_data ) {
      free( viewer->object_data );
   }
}

static void read_object_file( struct viewer* viewer ) {
   FILE* fh = fopen( viewer->options->file, "rb" );
   if ( ! fh ) {
      diag( viewer, DIAG_ERR,
         "failed to open file: %s", viewer->options->file );
      bail( viewer );
   }
   bool data_read = read_object_file_data( viewer, fh );
   fclose( fh );
   if ( ! data_read ) {
      bail( viewer );
   }
}

static bool read_object_file_data( struct viewer* viewer, FILE* fh ) {
   int seek_result = fseek( fh, 0, SEEK_END );
   if ( seek_result != 0 ) {
      diag( viewer, DIAG_ERR,
         "failed to seek to end of object file" );
      return false;
   }
   long tell_result = ftell( fh );
   if ( ! ( tell_result >= 0 ) ) {
      diag( viewer, DIAG_ERR,
         "failed to get size of object file" );
      return false;
   }
   seek_result = fseek( fh, 0, SEEK_SET );
   if ( seek_result != 0 ) {
      diag( viewer, DIAG_ERR,
         "failed to seek to beginning of object file" );
      return false;
   }
   if ( ( tell_result - INT_MAX ) > 0 ) {
      diag( viewer, DIAG_ERR,
         "object file too big (object file is %ld bytes, but the maximum "
         "supported object file size is %d bytes)", tell_result, INT_MAX );
      return false;
   }
   int size = ( int ) tell_result;
   // Plus one for a terminating NUL byte.
   unsigned char* data = malloc( sizeof( data[ 0 ] ) * size + 1 );
   size_t num_read = fread( data, sizeof( data[ 0 ] ), size, fh );
   data[ size ] = 0;
   viewer->object_data = data;
   viewer->object_size = size;
   bool success = false;
   if ( num_read == size ) {
      success = true;
   }
   else {
      diag( viewer, DIAG_ERR,
         "failed to read contents of object file" );
   }
   return success;
}

static bool perform_operation( struct viewer* viewer ) {
   struct object object;
   init_object( &object, viewer->object_data, viewer->object_size );
   determine_format( viewer, &object );
   determine_object_offsets( viewer, &object );
   const char* format = "ACSE";
   switch ( object.format ) {
   case FORMAT_BIG_E:
      break;
   case FORMAT_LITTLE_E:
      format = "ACSe";
      break;
   case FORMAT_ZERO:
      format = "ACS0";
      break;
   default:
      printf( "error: unsupported format\n" );
      return false;
   }
   const char* indirect = "";
   if ( object.indirect_format ) {
      indirect = " (indirect)";
   }
   printf( "format: %s%s\n", format, indirect );
   bool success = false;
   if ( viewer->options->list_chunks ) {
      switch ( object.format ) {
      case FORMAT_BIG_E:
      case FORMAT_LITTLE_E:
         list_chunks( viewer, &object );
         success = true;
         break;
      default:
         printf( "error: format does not support chunks\n" );
      }
   }
   else if ( viewer->options->view_chunk ) {
      switch ( object.format ) {
      case FORMAT_BIG_E:
      case FORMAT_LITTLE_E:
         if ( view_chunk( viewer, &object, viewer->options->view_chunk ) ) {
            success = true;
         }
         break;
      default:
         printf( "error: format does not support chunks\n" );
      }
   }
   else {
      show_object( viewer, &object );
      success = true;
   }
   return success;
}

static void init_object( struct object* object, const unsigned char* data,
   int size ) {
   object->data = data;
   object->size = size;
   object->format = FORMAT_UNKNOWN;
   object->directory_offset = 0;
   object->string_offset = 0;
   object->real_header_offset = 0;
   object->chunk_offset = 0;
   object->indirect_format = false;
   object->small_code = false;
}
 
static int data_left( struct object* object, const unsigned char* data ) {
   return ( int ) ( ( object->data + object->size ) - data );
}

static bool offset_in_range( struct object* object, const unsigned char* start,
   const unsigned char* end, int offset ) {
   return ( offset >= ( start - object->data ) &&
      offset < ( end - object->data ) );
}

static bool offset_in_object_file( struct object* object, int offset ) {
   return offset_in_range( object, object->data, object->data + object->size,
      offset );
}

static void expect_data( struct viewer* viewer, struct object* object,
   const unsigned char* start, int size ) {
   int left = data_left( object, start );
   if ( left < size ) {
      diag( viewer, DIAG_ERR,
         "expecting to read %d byte%s, "
         "but object file has %d byte%s of data left to read",
         size, ( size == 1 ) ? "" : "s",
         ( left < 0 ) ? 0 : left, ( left == 1 ) ? "" : "s" );
      bail( viewer );
   }
}

static void expect_offset_in_object_file( struct viewer* viewer,
   struct object* object, int offset ) {
   if ( ! offset_in_object_file( object, offset ) ) {
      diag( viewer, DIAG_ERR,
         "the object file appears to be malformed: an offset (%d) in the "
         "object file points outside the boundaries of the object file",
         offset );
      bail( viewer );
   }
}

static void expect_chunk_offset_in_chunk( struct viewer* viewer,
   struct chunk* chunk, int offset ) {
   if ( ! chunk_offset_in_chunk( chunk, offset ) ) {
      diag( viewer, DIAG_ERR,
         "an offset (%d) in %s chunk points outside the boundaries of the "
         "chunk", offset, chunk->name );
      bail( viewer );
   }
}

static void expect_chunk_data( struct viewer* viewer, struct chunk* chunk,
   const unsigned char* start, int size ) {
   int left = chunk_data_left( chunk, start );
   if ( left < size ) {
      diag( viewer, DIAG_ERR,
         "expecting to read %d byte%s, "
         "but %s chunk has %d byte%s of data left to read",
         size, ( size == 1 ) ? "" : "s", chunk->name,
         ( left < 0 ) ? 0 : left, ( left == 1 ) ? "" : "s" );
      bail( viewer );
   }
}

static int chunk_data_left( struct chunk* chunk, const unsigned char* data ) {
   return ( int ) ( ( chunk->data + chunk->size ) - data );
}

static bool chunk_offset_in_range( struct chunk* chunk,
   const unsigned char* start, const unsigned char* end, int offset ) {
   return ( offset >= ( start - chunk->data ) &&
      offset < ( end - chunk->data ) );
}

static bool chunk_offset_in_chunk( struct chunk* chunk, int offset ) {
   return chunk_offset_in_range( chunk, chunk->data, chunk->data + chunk->size,
      offset );
}

static void determine_format( struct viewer* viewer, struct object* object ) {
   struct header header;
   if ( data_left( object, object->data ) < sizeof( header ) ) {
      diag( viewer, DIAG_ERR,
         "object file too small to be an ACS object file" );
      bail( viewer );
   }
   memcpy( &header, object->data, sizeof( header ) );
   expect_offset_in_object_file( viewer, object, header.offset );
   object->directory_offset = header.offset;
   if ( memcmp( header.id, "ACSE", 4 ) == 0 ||
      memcmp( header.id, "ACSe", 4 ) == 0 ) {
      object->format = ( header.id[ 3 ] == 'E' ) ?
         FORMAT_BIG_E : FORMAT_LITTLE_E;
      object->chunk_offset = object->directory_offset;
   }
   else if ( memcmp( header.id, "ACS\0", 4 ) == 0 ) {
      // ACSE/ACSe object file disguised as ACS0 object file.
      if ( peek_real_id( object, &header ) ) {
         int offset = header.offset - sizeof( header.id );
         object->format = ( ( object->data + offset )[ 3 ] == 'E' ) ?
            FORMAT_BIG_E : FORMAT_LITTLE_E;
         int chunk_offset = 0;
         offset -= sizeof( chunk_offset );
         expect_offset_in_object_file( viewer, object, offset );
         memcpy( &chunk_offset, object->data + offset,
            sizeof( chunk_offset ) );
         object->real_header_offset = offset;
         object->chunk_offset = chunk_offset;
         object->indirect_format = true;
      }
      else {
         object->format = FORMAT_ZERO;
      }
   }
   if ( object->format == FORMAT_LITTLE_E ) {
      object->small_code = true;
   }
}

static bool peek_real_id( struct object* object, struct header* header ) {
   int offset = header->offset - sizeof( header->id );
   if ( offset_in_object_file( object, offset ) ) {
      const unsigned char* data = object->data + offset;
      if ( memcmp( data, "ACSE", 4 ) == 0 ||
         memcmp( data, "ACSe", 4 ) == 0 ) {
         return true;
      }
   }
   return false;
}

static void determine_object_offsets( struct viewer* viewer,
   struct object* object ) {
   if ( script_directory_present( object ) ) {
      const unsigned char* data = object->data + object->directory_offset;
      int total_scripts = 0;
      expect_data( viewer, object, data, sizeof( total_scripts ) );
      memcpy( &total_scripts, data, sizeof( total_scripts ) );
      int string_offset = object->directory_offset + sizeof( total_scripts ) +
         ( total_scripts * sizeof( struct acs0_script_entry ) );
      expect_offset_in_object_file( viewer, object, string_offset );
      object->string_offset = string_offset;
   }
}

static bool script_directory_present( struct object* object ) {
   switch ( object->format ) {
   case FORMAT_BIG_E:
   case FORMAT_LITTLE_E:
      // Only the indirect formats have the script and string directories
      // because they are also ACS0 files. The direct formats do not have these
      // directories.  
      return ( object->indirect_format == true );
   case FORMAT_ZERO:
      return true;
   default:
      return false;
   }
}

static void list_chunks( struct viewer* viewer, struct object* object ) {
   struct chunk chunk;
   struct chunk_reader reader;
   init_chunk_reader( &reader, object );
   while ( read_chunk( viewer, &reader, &chunk ) ) {
      show_chunk( viewer, object, &chunk, false );
   }
}

static bool show_chunk( struct viewer* viewer, struct object* object,
   struct chunk* chunk, bool show_contents ) {
   printf( "-- %s (offset=%d size=%d)\n", chunk->name,
      ( int ) ( ( chunk->data - sizeof( struct chunk_header ) ) -
         object->data ), chunk->size );
   if ( show_contents ) {
      switch ( chunk->type ) {
      case CHUNK_ARAY:
         show_aray( viewer, chunk );
         break;
      case CHUNK_AINI:
         show_aini( viewer, chunk );
         break;
      case CHUNK_AIMP:
         show_aimp( viewer, chunk );
         break;
      case CHUNK_ASTR:
      case CHUNK_MSTR:
         show_astr_mstr( viewer, chunk );
         break;
      case CHUNK_ATAG:
         show_atag( viewer, chunk );
         break;
      case CHUNK_LOAD:
         show_load( viewer, chunk );
         break;
      case CHUNK_FUNC:
         show_func( viewer, object, chunk );
         break;
      case CHUNK_FNAM:
         show_fnam( viewer, chunk );
         break;
      case CHUNK_MINI:
         show_mini( viewer, chunk );
         break;
      case CHUNK_MIMP:
         show_mimp( viewer, chunk );
         break;
      case CHUNK_MEXP:
         show_mexp( viewer, chunk );
         break;
      case CHUNK_SPTR:
         show_sptr( viewer, object, chunk );
         break;
      case CHUNK_SFLG:
         show_sflg( viewer, chunk );
         break;
      case CHUNK_SVCT:
         show_svct( viewer, chunk );
         break;
      case CHUNK_SNAM:
         show_snam( viewer, chunk );
         break;
      case CHUNK_STRL:
      case CHUNK_STRE:
         show_strl_stre( viewer, chunk );
         break;
      case CHUNK_SARY:
      case CHUNK_FARY:
         show_sary_fary( viewer, chunk );
         break;
      case CHUNK_ALIB:
         show_alib( chunk );
         break;
      default:
         printf( "chunk not supported\n" ); 
         break;
      }
   }
   return true;
}

static void show_aray( struct viewer* viewer, struct chunk* chunk ) {
   int pos = 0;
   while ( pos < chunk->size ) {
      struct {
         int number;
         int size;
      } entry;
      expect_chunk_data( viewer, chunk, chunk->data + pos, sizeof( entry ) );
      memcpy( &entry, chunk->data + pos, sizeof( entry ) );
      printf( "index=%d size=%d\n", entry.number, entry.size );
      pos += sizeof( entry );
   }
}

static void show_aini( struct viewer* viewer, struct chunk* chunk ) {
   int pos = 0;
   int index = 0;
   expect_chunk_data( viewer, chunk, chunk->data + pos, sizeof( index ) );
   memcpy( &index, chunk->data + pos, sizeof( index ) );
   pos += sizeof( index );
   printf( "array-index=%d\n", index );
   while ( pos < chunk->size ) {
      int value = 0;
      expect_chunk_data( viewer, chunk, chunk->data + pos, sizeof( value ) );
      memcpy( &value, chunk->data + pos, sizeof( value ) );
      int element = ( pos - sizeof( index ) ) / sizeof( value );
      printf( "[%d] = %d\n", element, value );
      pos += sizeof( value );
   }
}

static void show_aimp( struct viewer* viewer, struct chunk* chunk ) {
   const unsigned char* data = chunk->data;
   int total_arrays = 0;
   expect_chunk_data( viewer, chunk, data, sizeof( total_arrays ) );
   memcpy( &total_arrays, data, sizeof( total_arrays ) );
   data += sizeof( total_arrays );
   printf( "total-imported-arrays=%d\n", total_arrays );
   int i = 0;
   while ( i < total_arrays ) {
      unsigned int index = 0;
      expect_chunk_data( viewer, chunk, data, sizeof( index ) );
      memcpy( &index, data, sizeof( index ) );
      data += sizeof( index );
      unsigned int size = 0;
      expect_chunk_data( viewer, chunk, data, sizeof( size ) );
      memcpy( &size, data, sizeof( size ) );
      data += sizeof( size );
      const char* string = read_chunk_string( viewer, chunk,
         ( int ) ( data - chunk->data ) );
      printf( "index=%u %s[%u]\n", index, string, size );
      data += strlen( string ) + 1; // Plus one for NUL character.
      ++i;
   }
}

static void show_astr_mstr( struct viewer* viewer, struct chunk* chunk ) {
   int pos = 0;
   while ( pos < chunk->size ) {
      unsigned int index = 0;
      expect_chunk_data( viewer, chunk, chunk->data + pos, sizeof( index ) );
      memcpy( &index, chunk->data + pos, sizeof( index ) );
      printf( "tagged=%u\n", index );
      pos += sizeof( index );
   }
}

static void show_atag( struct viewer* viewer, struct chunk* chunk ) {
   unsigned char version = 0;
   expect_chunk_data( viewer, chunk, chunk->data, sizeof( version ) );
   memcpy( &version, chunk->data, sizeof( version ) );
   switch ( version ) {
   case 0:
      show_atag_version0( viewer, chunk );
      break;
   default:
      printf( "chunk-version=%d\n", version );
      printf( "this version not supported\n" );
   }
}

static void show_atag_version0( struct viewer* viewer, struct chunk* chunk ) {
   const unsigned char* data = chunk->data;
   unsigned char version = 0;
   expect_chunk_data( viewer, chunk, data, sizeof( version ) );
   memcpy( &version, data, sizeof( version ) );
   data += sizeof( version );
   int index = 0;
   expect_chunk_data( viewer, chunk, data, sizeof( index ) );
   memcpy( &index, data, sizeof( index ) );
   data += sizeof( index );
   unsigned char tag = 0;
   int total_tags = ( chunk->size - sizeof( version ) - sizeof( index ) ) /
      sizeof( tag );
   printf( "chunk-version=%d tagged-array=%d total-tagged-elements=%d\n",
      version, index, total_tags );
   for ( int i = 0; i < total_tags; ++i ) {
      memcpy( &tag, data, sizeof( tag ) );
      data += sizeof( tag );
      enum {
         TAG_INTEGER,
         TAG_STRING,
         TAG_FUNCTION,
      };
      printf( "[%d] ", i );
      switch ( tag ) {
      case TAG_INTEGER:
         printf( "integer" );
         break;
      case TAG_STRING:
         printf( "string" );
         break;
      case TAG_FUNCTION:
         printf( "function" );
         break;
      default:
         printf( "unknown (tag-type=%d)", tag );
      }
      printf( "\n" );
   }
}

static void show_load( struct viewer* viewer, struct chunk* chunk ) {
   int pos = 0;
   while ( pos < chunk->size ) {
      const char* name = read_chunk_string( viewer, chunk, pos );
      if ( name[ 0 ] != '\0' ) {
         printf( "imported-module=%s\n", name );
      }
      pos += strlen( name ) + 1; // Plus one for NUL character.
   }
}

static void show_func( struct viewer* viewer, struct object* object,
   struct chunk* chunk ) {
   struct func_entry entry;
   int total_funcs = chunk->size / sizeof( entry );
   for ( int i = 0; i < total_funcs; ++i ) {
      memcpy( &entry, chunk->data + i * sizeof( entry ), sizeof( entry ) );
      printf( "index=%d params=%d size=%d has-return=%d offset=%d\n", i,
         entry.num_param, entry.size, entry.has_return, entry.offset );
      if ( offset_in_object_file( object, entry.offset ) ) {
         if ( entry.offset != 0 ) {
            show_pcode( viewer, object, entry.offset,
               calc_code_size( viewer, object, entry.offset ) );
         }
         else {
            printf( "(imported)\n" );
         }
      }
      else {
         diag( viewer, DIAG_WARN,
            "offset (%d) points outside the object file, so the function code "
            "will not be shown", entry.offset );  
      }
   }
}

static void show_fnam( struct viewer* viewer, struct chunk* chunk ) {
   const unsigned char* data = chunk->data;
   int total_names = 0;
   expect_chunk_data( viewer, chunk, data, sizeof( total_names ) );
   memcpy( &total_names, data, sizeof( total_names ) );
   data += sizeof( total_names );
   printf( "total-names=%d\n", total_names );
   for ( int i = 0; i < total_names; ++i ) {
      int offset = 0;
      expect_chunk_data( viewer, chunk, data, sizeof( offset ) );
      memcpy( &offset, data, sizeof( offset ) );
      data += sizeof( offset );
      expect_chunk_offset_in_chunk( viewer, chunk, offset );
      printf( "[%d] offset=%d %s\n", i, offset,
         read_chunk_string( viewer, chunk, offset ) );
   }
}

static void show_mini( struct viewer* viewer, struct chunk* chunk ) {
   const unsigned char* data = chunk->data;
   int first_var = 0;
   expect_chunk_data( viewer, chunk, data, sizeof( first_var ) );
   memcpy( &first_var, data, sizeof( first_var ) );
   data += sizeof( first_var );
   printf( "first-var=%d\n", first_var );
   int value = 0;
   int total_values = ( chunk->size - sizeof( first_var ) ) / sizeof( value );
   for ( int i = 0; i < total_values; ++i ) {
      memcpy( &value, data, sizeof( value ) );
      data += sizeof( value );
      printf( "index=%d value=%d\n", first_var + i, value );
   }
}

static void show_mimp( struct viewer* viewer, struct chunk* chunk ) {
   int pos = 0;
   while ( pos < chunk->size ) {
      int index = 0;
      expect_chunk_data( viewer, chunk, chunk->data + pos, sizeof( index ) );
      memcpy( &index, chunk->data + pos, sizeof( index ) );
      pos += sizeof( index );
      const char* name = read_chunk_string( viewer, chunk, pos );
      printf( "index=%d name=%s\n", index, name );
      pos += strlen( name ) + 1; // Plus one for NUL character.
   }
}

static void show_mexp( struct viewer* viewer, struct chunk* chunk ) {
   const unsigned char* data = chunk->data;
   int total_names = 0;
   expect_chunk_data( viewer, chunk, data, sizeof( total_names ) );
   memcpy( &total_names, data, sizeof( total_names ) );
   data += sizeof( total_names );
   printf( "table-size=%d\n", total_names );
   for ( int i = 0; i < total_names; ++i ) {
      int offset = 0;
      expect_chunk_data( viewer, chunk, data, sizeof( offset ) );
      memcpy( &offset, data, sizeof( offset ) );
      data += sizeof( offset );
      expect_chunk_offset_in_chunk( viewer, chunk, offset );
      printf( "[%d] offset=%d %s\n", i, offset,
         read_chunk_string( viewer, chunk, offset ) );
   }
}

static void show_sptr( struct viewer* viewer, struct object* object,
   struct chunk* chunk ) {
   int pos = 0;
   while ( pos < chunk->size ) {
      struct common_acse_script_entry entry;
      read_acse_script_entry( viewer, object, chunk, chunk->data + pos,
         &entry );
      pos += entry.real_entry_size;
      printf( "script=%d ", entry.number );
      const char* name = get_script_type_name( entry.type );
      if ( name ) {
         printf( "type=%s ", name );
      }
      else {
         printf( "type=unknown:%d ", entry.type );
      }
      printf( "params=%d offset=%d\n", entry.num_param, entry.offset );
      if ( offset_in_object_file( object, entry.offset ) ) {
         show_pcode( viewer, object, entry.offset,
            calc_code_size( viewer, object, entry.offset ) );
      }
      else {
         diag( viewer, DIAG_WARN,
            "offset (%d) points outside the object file, so the script code "
            "will not be shown", entry.offset );  
      }
   }
}

static void read_acse_script_entry( struct viewer* viewer,
   struct object* object, struct chunk* chunk, const unsigned char* data,
   struct common_acse_script_entry* common_entry ) {
   if ( object->indirect_format ) {
      struct {
         short number;
         unsigned char type;
         unsigned char num_param;
         int offset;
      } entry;
      expect_chunk_data( viewer, chunk, data, sizeof( entry ) );
      memcpy( &entry, data, sizeof( entry ) );
      common_entry->number = entry.number;
      common_entry->type = entry.type;
      common_entry->num_param = entry.num_param;
      common_entry->offset = entry.offset;
      common_entry->real_entry_size = sizeof( entry );
   }
   else {
      struct {
         short number;
         short type;
         int offset;
         int num_param;
      } entry;
      expect_chunk_data( viewer, chunk, data, sizeof( entry ) );
      memcpy( &entry, data, sizeof( entry ) );
      common_entry->number = entry.number;
      common_entry->type = entry.type;
      common_entry->num_param = entry.num_param;
      common_entry->offset = entry.offset;
      common_entry->real_entry_size = sizeof( entry );
   }
}

static int calc_code_size( struct viewer* viewer, struct object* object,
   int offset ) {
   int end_offset = object->size;
   // The starting offset of an adjacent script can be used as the end offset.
   if (
      object->format == FORMAT_BIG_E ||
      object->format == FORMAT_LITTLE_E ) {
      struct chunk chunk;
      if ( find_chunk( viewer, object, "SPTR", &chunk ) ) {
         int size = 0;
         while ( size < chunk.size ) {
            struct common_acse_script_entry entry;
            read_acse_script_entry( viewer, object, &chunk, chunk.data + size,
               &entry );
            size += entry.real_entry_size;
            if ( entry.offset > offset && entry.offset < end_offset ) {
               end_offset = entry.offset;
            }
         }
      }
      // The starting offset of a function can be used as the end offset.
      if ( find_chunk( viewer, object, "FUNC", &chunk ) ) {
         struct func_entry entry;
         int total_funcs = chunk.size / sizeof( entry );
         for ( int i = 0; i < total_funcs; ++i ) {
            memcpy( &entry, chunk.data + i * sizeof( entry ),
               sizeof( entry ) );
            if ( entry.offset > offset && entry.offset < end_offset ) {
               end_offset = entry.offset;
            }
         }
      }
   }
   // The starting offset of a script in the script directory can be used as
   // the end offset.
   if ( script_directory_present( object ) ) {
      const unsigned char* data = object->data + object->directory_offset;
      int count = 0;
      memcpy( &count, data, sizeof( count ) );
      data += sizeof( count );
      for ( int i = 0; i < count; ++i ) {
         struct acs0_script_entry entry;
         memcpy( &entry, data, sizeof( entry ) );
         data += sizeof( entry );
         if ( entry.offset > offset && entry.offset < end_offset ) {
            end_offset = entry.offset;
         }
      }
      // The offset of a string in the string directory can be used as the end
      // offset.
      data = object->data + object->string_offset;
      memcpy( &count, data, sizeof( count ) );
      data += sizeof( count );
      for ( int i = 0; i < count; ++i ) {
         int string_offset = 0;
         memcpy( &string_offset, data, sizeof( string_offset ) );
         data += sizeof( string_offset );
         if ( string_offset > offset && string_offset < end_offset ) {
            end_offset = string_offset;
         }
      }
   }
   // For the last script, the chunk section offset can be used as the end
   // offset.
   if (
      object->format == FORMAT_BIG_E ||
      object->format == FORMAT_LITTLE_E ) {
      if ( object->chunk_offset < end_offset ) {
         end_offset = object->chunk_offset;
      }
   }
   // For the last script, the script directory offset can be used as the end
   // offset.
   if ( script_directory_present( object ) &&
      object->directory_offset < end_offset ) {
      end_offset = object->directory_offset;
   }
   return end_offset - offset;
}

static const char* get_script_type_name( int type ) {
   enum {
      TYPE_CLOSED,
      TYPE_OPEN,
      TYPE_RESPAWN,
      TYPE_DEATH,
      TYPE_ENTER,
      TYPE_PICKUP,
      TYPE_BLUERETURN,
      TYPE_REDRETURN,
      TYPE_WHITERETURN,
      TYPE_LIGHTNING = 12,
      TYPE_UNLOADING,
      TYPE_DISCONNECT,
      TYPE_RETURN,
      TYPE_EVENT,
      TYPE_KILL,
      TYPE_REOPEN,
   };
   switch ( type ) {
   case TYPE_CLOSED: return "closed";
   case TYPE_OPEN: return "open";
   case TYPE_RESPAWN: return "respawn";
   case TYPE_DEATH: return "death";
   case TYPE_ENTER: return "enter";
   case TYPE_PICKUP: return "pickup";
   case TYPE_BLUERETURN: return "bluereturn";
   case TYPE_REDRETURN: return "redreturn";
   case TYPE_WHITERETURN: return "whitereturn";
   case TYPE_LIGHTNING: return "lightning";
   case TYPE_UNLOADING: return "unloading";
   case TYPE_DISCONNECT: return "disconnect";
   case TYPE_RETURN: return "return";
   case TYPE_EVENT: return "event";
   case TYPE_KILL: return "kill";
   case TYPE_REOPEN: return "reopen";
   default: return NULL;
   }
}

static void show_pcode( struct viewer* viewer, struct object* object,
   int offset, int code_size ) {
   struct pcode_segment segment;
   init_pcode_segment( object, &segment, offset, code_size );
   while ( ! pcode_segment_end( &segment ) ) {
      show_instruction( viewer, object, &segment );
   }
}

static void init_pcode_segment( struct object* object,
   struct pcode_segment* segment, int offset, int code_size ) {
   segment->data_start = object->data + offset;
   segment->data = segment->data_start;
   segment->offset = offset;
   segment->code_size = code_size;
   segment->opcode = PCD_NOP;
   segment->invalid_opcode = false;
}

static bool pcode_segment_end( struct pcode_segment* segment ) {
   return ( ( segment->data - segment->data_start >= segment->code_size ) ||
      segment->invalid_opcode );
}

static void expect_pcode_data( struct viewer* viewer,
   struct pcode_segment* segment, int size ) {
   int left = segment->code_size - ( segment->data - segment->data_start );
   if ( left < size ) {
      diag( viewer, DIAG_ERR,
         "expecting to read %d byte%s of pcode data, "
         "but this pcode segment has %d byte%s of data left to read",
         size, ( size == 1 ) ? "" : "s",
         ( left < 0 ) ? 0 : left, ( left == 1 ) ? "" : "s" );
      bail( viewer );
   }
}

static void show_instruction( struct viewer* viewer, struct object* object,
   struct pcode_segment* segment ) {
   show_opcode( viewer, object, segment );
   if ( ! segment->invalid_opcode ) {
      show_args( viewer, object, segment );
   }
}

static void show_opcode( struct viewer* viewer, struct object* object,
   struct pcode_segment* segment ) {
   int pos = segment->offset + ( int ) ( segment->data - segment->data_start );
   int opcode = PCD_NOP;
   if ( object->small_code ) {
      unsigned char temp = 0;
      expect_pcode_data( viewer, segment, sizeof( temp ) );
      memcpy( &temp, segment->data, sizeof( temp ) );
      segment->data += sizeof( temp );
      opcode = temp;
      if ( temp >= 240 ) {
         expect_pcode_data( viewer, segment, sizeof( temp ) );
         memcpy( &temp, segment->data, sizeof( temp ) );
         segment->data += sizeof( temp );
         opcode += temp;
      }
   }
   else {
      expect_pcode_data( viewer, segment, sizeof( opcode ) );
      memcpy( &opcode, segment->data, sizeof( opcode ) );
      segment->data += sizeof( opcode );
   }
   printf( "%08d> ", pos );
   if ( opcode >= PCD_NOP && opcode < PCD_TOTAL ) {
      printf( "%s", g_pcodes[ opcode ].name );
      segment->opcode = opcode;
   }
   else {
      printf( "unknown pcode: %d\n", opcode );
      segment->invalid_opcode = true;
   }
}

static void show_args( struct viewer* viewer, struct object* object,
   struct pcode_segment* segment ) {
   switch ( segment->opcode ) {
   // One-argument instructions. Argument can be 1 byte or 4 bytes.
   case PCD_LSPEC1:
   case PCD_LSPEC2:
   case PCD_LSPEC3:
   case PCD_LSPEC4:
   case PCD_LSPEC5:
   case PCD_ASSIGNSCRIPTVAR:
   case PCD_ASSIGNMAPVAR:
   case PCD_ASSIGNWORLDVAR:
   case PCD_PUSHSCRIPTVAR:
   case PCD_PUSHMAPVAR:
   case PCD_PUSHWORLDVAR:
   case PCD_ADDSCRIPTVAR:
   case PCD_ADDMAPVAR:
   case PCD_ADDWORLDVAR:
   case PCD_SUBSCRIPTVAR:
   case PCD_SUBMAPVAR:
   case PCD_SUBWORLDVAR:
   case PCD_MULSCRIPTVAR:
   case PCD_MULMAPVAR:
   case PCD_MULWORLDVAR:
   case PCD_DIVSCRIPTVAR:
   case PCD_DIVMAPVAR:
   case PCD_DIVWORLDVAR:
   case PCD_MODSCRIPTVAR:
   case PCD_MODMAPVAR:
   case PCD_MODWORLDVAR:
   case PCD_INCSCRIPTVAR:
   case PCD_INCMAPVAR:
   case PCD_INCWORLDVAR:
   case PCD_DECSCRIPTVAR:
   case PCD_DECMAPVAR:
   case PCD_DECWORLDVAR:
   case PCD_ASSIGNGLOBALVAR:
   case PCD_PUSHGLOBALVAR:
   case PCD_ADDGLOBALVAR:
   case PCD_SUBGLOBALVAR:
   case PCD_MULGLOBALVAR:
   case PCD_DIVGLOBALVAR:
   case PCD_MODGLOBALVAR:
   case PCD_INCGLOBALVAR:
   case PCD_DECGLOBALVAR:
   case PCD_CALL:
   case PCD_CALLDISCARD:
   case PCD_PUSHMAPARRAY:
   case PCD_ASSIGNMAPARRAY:
   case PCD_ADDMAPARRAY:
   case PCD_SUBMAPARRAY:
   case PCD_MULMAPARRAY:
   case PCD_DIVMAPARRAY:
   case PCD_MODMAPARRAY:
   case PCD_INCMAPARRAY:
   case PCD_DECMAPARRAY:
   case PCD_PUSHWORLDARRAY:
   case PCD_ASSIGNWORLDARRAY:
   case PCD_ADDWORLDARRAY:
   case PCD_SUBWORLDARRAY:
   case PCD_MULWORLDARRAY:
   case PCD_DIVWORLDARRAY:
   case PCD_MODWORLDARRAY:
   case PCD_INCWORLDARRAY:
   case PCD_DECWORLDARRAY:
   case PCD_PUSHGLOBALARRAY:
   case PCD_ASSIGNGLOBALARRAY:
   case PCD_ADDGLOBALARRAY:
   case PCD_SUBGLOBALARRAY:
   case PCD_MULGLOBALARRAY:
   case PCD_DIVGLOBALARRAY:
   case PCD_MODGLOBALARRAY:
   case PCD_INCGLOBALARRAY:
   case PCD_DECGLOBALARRAY:
   case PCD_LSPEC5RESULT:
   case PCD_ANDSCRIPTVAR:
   case PCD_ANDMAPVAR:
   case PCD_ANDGLOBALVAR:
   case PCD_ANDMAPARRAY:
   case PCD_ANDWORLDARRAY:
   case PCD_ANDGLOBALARRAY:
   case PCD_EORSCRIPTVAR:
   case PCD_EORMAPVAR:
   case PCD_EORWORLDVAR:
   case PCD_EORGLOBALVAR:
   case PCD_EORMAPARRAY:
   case PCD_EORWORLDARRAY:
   case PCD_EORGLOBALARRAY:
   case PCD_ORSCRIPTVAR:
   case PCD_ORMAPVAR:
   case PCD_ORWORLDVAR:
   case PCD_ORGLOBALVAR:
   case PCD_ORMAPARRAY:
   case PCD_ORWORLDARRAY:
   case PCD_ORGLOBALARRAY:
   case PCD_LSSCRIPTVAR:
   case PCD_LSMAPVAR:
   case PCD_LSWORLDVAR:
   case PCD_LSGLOBALVAR:
   case PCD_LSMAPARRAY:
   case PCD_LSWORLDARRAY:
   case PCD_LSGLOBALARRAY:
   case PCD_RSSCRIPTVAR:
   case PCD_RSMAPVAR:
   case PCD_RSWORLDVAR:
   case PCD_RSGLOBALVAR:
   case PCD_RSMAPARRAY:
   case PCD_RSWORLDARRAY:
   case PCD_RSGLOBALARRAY:
   case PCD_PUSHFUNCTION:
   case PCD_ASSIGNSCRIPTARRAY:
   case PCD_PUSHSCRIPTARRAY:
   case PCD_ADDSCRIPTARRAY:
   case PCD_SUBSCRIPTARRAY:
   case PCD_MULSCRIPTARRAY:
   case PCD_DIVSCRIPTARRAY:
   case PCD_MODSCRIPTARRAY:
   case PCD_INCSCRIPTARRAY:
   case PCD_DECSCRIPTARRAY:
   case PCD_ANDSCRIPTARRAY:
   case PCD_EORSCRIPTARRAY:
   case PCD_ORSCRIPTARRAY:
   case PCD_LSSCRIPTARRAY:
   case PCD_RSSCRIPTARRAY:
      {
         int arg = 0;
         if ( object->small_code ) {
            unsigned char temp = 0;
            expect_pcode_data( viewer, segment, sizeof( temp ) );
            memcpy( &temp, segment->data, sizeof( temp ) );
            segment->data += sizeof( temp );
            arg = temp;
         }
         else {
            expect_pcode_data( viewer, segment, sizeof( arg ) );
            memcpy( &arg, segment->data, sizeof( arg ) );
            segment->data += sizeof( arg );
         }
         printf( " %d\n", arg );
      }
      break;
   case PCD_LSPEC1DIRECT:
      {
         int id = 0;
         if ( object->small_code ) {
            expect_pcode_data( viewer, segment, sizeof( *segment->data ) );
            id = *segment->data;
            ++segment->data;
         }
         else {
            expect_pcode_data( viewer, segment, sizeof( id ) );
            memcpy( &id, segment->data, sizeof( id ) );
            segment->data += sizeof( id );
         }
         int arg = 0;
         expect_pcode_data( viewer, segment, sizeof( arg ) );
         memcpy( &arg, segment->data, sizeof( arg ) );
         segment->data += sizeof( arg );
         printf( " %d %d\n", id, arg );
      }
      break;
   case PCD_LSPEC2DIRECT:
      {
         int id = 0;
         if ( object->small_code ) {
            expect_pcode_data( viewer, segment, sizeof( *segment->data ) );
            id = *segment->data;
            ++segment->data;
         }
         else {
            expect_pcode_data( viewer, segment, sizeof( id ) );
            memcpy( &id, segment->data, sizeof( id ) );
            segment->data += sizeof( id );
         }
         int args[ 2 ];
         expect_pcode_data( viewer, segment, sizeof( args ) );
         memcpy( args, segment->data, sizeof( args ) );
         segment->data += sizeof( args );
         printf( " %d %d %d\n",
            id,
            args[ 0 ],
            args[ 1 ] );
      }
      break;
   case PCD_LSPEC3DIRECT:
      {
         int id = 0;
         if ( object->small_code ) {
            expect_pcode_data( viewer, segment, sizeof( *segment->data ) );
            id = *segment->data;
            ++segment->data;
         }
         else {
            expect_pcode_data( viewer, segment, sizeof( id ) );
            memcpy( &id, segment->data, sizeof( id ) );
            segment->data += sizeof( id );
         }
         int args[ 3 ];
         expect_pcode_data( viewer, segment, sizeof( args ) );
         memcpy( args, segment->data, sizeof( args ) );
         segment->data += sizeof( args );
         printf( " %d %d %d %d\n",
            id,
            args[ 0 ],
            args[ 1 ],
            args[ 2 ] );
      }
      break;
   case PCD_LSPEC4DIRECT:
      {
         int id = 0;
         if ( object->small_code ) {
            expect_pcode_data( viewer, segment, sizeof( *segment->data ) );
            id = *segment->data;
            ++segment->data;
         }
         else {
            expect_pcode_data( viewer, segment, sizeof( id ) );
            memcpy( &id, segment->data, sizeof( id ) );
            segment->data += sizeof( id );
         }
         int args[ 4 ];
         expect_pcode_data( viewer, segment, sizeof( args ) );
         memcpy( args, segment->data, sizeof( args ) );
         segment->data += sizeof( args );
         printf( " %d %d %d %d %d\n",
            id,
            args[ 0 ],
            args[ 1 ],
            args[ 2 ],
            args[ 3 ] );
      }
      break;
   case PCD_LSPEC5DIRECT:
      {
         int id = 0;
         if ( object->small_code ) {
            expect_pcode_data( viewer, segment, sizeof( *segment->data ) );
            id = *segment->data;
            ++segment->data;
         }
         else {
            expect_pcode_data( viewer, segment, sizeof( id ) );
            memcpy( &id, segment->data, sizeof( id ) );
            segment->data += sizeof( id );
         }
         int args[ 5 ];
         expect_pcode_data( viewer, segment, sizeof( args ) );
         memcpy( args, segment->data, sizeof( args ) );
         segment->data += sizeof( args );
         printf( " %d %d %d %d %d %d\n",
            id,
            args[ 0 ],
            args[ 1 ],
            args[ 2 ],
            args[ 3 ],
            args[ 4 ] );
      }
      break;
   case PCD_LSPEC1DIRECTB:
      expect_pcode_data( viewer, segment, sizeof( segment->data[ 0 ] ) * 2 );
      printf( " %hhu %hhu\n",
         segment->data[ 0 ],
         segment->data[ 1 ] );
      segment->data += sizeof( segment->data[ 0 ] ) * 2;
      break;
   case PCD_LSPEC2DIRECTB:
      expect_pcode_data( viewer, segment, sizeof( segment->data[ 0 ] ) * 3 );
      printf( " %hhu %hhu %hhu\n",
         segment->data[ 0 ],
         segment->data[ 1 ],
         segment->data[ 2 ] );
      segment->data += sizeof( segment->data[ 0 ] ) * 3;
      break;
   case PCD_LSPEC3DIRECTB:
      expect_pcode_data( viewer, segment, sizeof( segment->data[ 0 ] ) * 4 );
      printf( " %hhu %hhu %hhu %hhu\n",
         segment->data[ 0 ],
         segment->data[ 1 ],
         segment->data[ 2 ],
         segment->data[ 3 ] );
      segment->data += sizeof( segment->data[ 0 ] ) * 4;
      break;
   case PCD_LSPEC4DIRECTB:
      expect_pcode_data( viewer, segment, sizeof( segment->data[ 0 ] ) * 5 );
      printf( " %hhu %hhu %hhu %hhu %hhu\n",
         segment->data[ 0 ],
         segment->data[ 1 ],
         segment->data[ 2 ],
         segment->data[ 3 ],
         segment->data[ 4 ] );
      segment->data += sizeof( segment->data[ 0 ] ) * 5;
      break;
   case PCD_LSPEC5DIRECTB:
      expect_pcode_data( viewer, segment, sizeof( segment->data[ 0 ] ) * 6 );
      printf( " %hhu %hhu %hhu %hhu %hhu %hhu\n",
         segment->data[ 0 ],
         segment->data[ 1 ],
         segment->data[ 2 ],
         segment->data[ 3 ],
         segment->data[ 4 ],
         segment->data[ 5 ] );
      segment->data += sizeof( segment->data[ 0 ] ) * 6;
      break;
   case PCD_PUSHBYTE:
   case PCD_DELAYDIRECTB:
      expect_pcode_data( viewer, segment, sizeof( *segment->data ) );
      printf( " %hhu\n", *segment->data );
      segment->data += sizeof( *segment->data );
      break;
   case PCD_PUSH2BYTES:
   case PCD_RANDOMDIRECTB:
      expect_pcode_data( viewer, segment, sizeof( segment->data[ 0 ] ) * 2 );
      printf( " %hhu %hhu\n",
         segment->data[ 0 ],
         segment->data[ 1 ] );
      segment->data += sizeof( segment->data[ 0 ] ) * 2;
      break;
   case PCD_PUSH3BYTES:
      expect_pcode_data( viewer, segment, sizeof( segment->data[ 0 ] ) * 3 );
      printf( " %hhu %hhu %hhu\n",
         segment->data[ 0 ],
         segment->data[ 1 ],
         segment->data[ 2 ] );
      segment->data += sizeof( segment->data[ 0 ] ) * 3;
      break;
   case PCD_PUSH4BYTES:
      expect_pcode_data( viewer, segment, sizeof( segment->data[ 0 ] ) * 4 );
      printf( " %hhu %hhu %hhu %hhu\n",
         segment->data[ 0 ],
         segment->data[ 1 ],
         segment->data[ 2 ],
         segment->data[ 3 ] );
      segment->data += sizeof( segment->data[ 0 ] ) * 4;
      break;
   case PCD_PUSH5BYTES:
      expect_pcode_data( viewer, segment, sizeof( segment->data[ 0 ] ) * 5 );
      printf( " %hhu %hhu %hhu %hhu %hhu\n",
         segment->data[ 0 ],
         segment->data[ 1 ],
         segment->data[ 2 ],
         segment->data[ 3 ],
         segment->data[ 4 ] );
      segment->data += sizeof( segment->data[ 0 ] ) * 5;
      break;
   case PCD_PUSHBYTES:
      {
         expect_pcode_data( viewer, segment, sizeof( *segment->data ) );
         int count = *segment->data;
         ++segment->data;
         printf( " count=%d", count );
         expect_pcode_data( viewer, segment,
            sizeof( segment->data[ 0 ] ) * count );
         for ( int i = 0; i < count; ++i ) {
            printf( " %hhu", *segment->data );
            ++segment->data;
         }
         printf( "\n" );
      }
      break;
   case PCD_CASEGOTOSORTED:
      {
         // Count and cases are 4-byte aligned.
         int remainder = ( segment->offset +
            ( segment->data - segment->data_start ) ) % sizeof( int );
         if ( remainder > 0 ) {
            int padding = sizeof( int ) - remainder;
            expect_pcode_data( viewer, segment, padding );
            segment->data += padding;
         }
         int count = 0;
         expect_pcode_data( viewer, segment, sizeof( count ) );
         memcpy( &count, segment->data, sizeof( count ) );
         segment->data += sizeof( count );
         printf( " num-cases=%d\n", count );
         for ( int i = 0; i < count; ++i ) {
            int value = 0;
            expect_pcode_data( viewer, segment, sizeof( value ) );
            memcpy( &value, segment->data, sizeof( value ) );
            segment->data += sizeof( value );
            printf( "%08d>   case %d: ", segment->offset +
               ( int ) ( segment->data - segment->data_start ), value );
            int offset = 0;
            expect_pcode_data( viewer, segment, sizeof( offset ) );
            memcpy( &offset, segment->data, sizeof( offset ) );
            segment->data += sizeof( offset );
            printf( "%d\n", offset );
         }
      }
      break;
   case PCD_CALLFUNC:
      {
         int num_args = 0;
         if ( object->small_code ) {
            unsigned char temp = 0;
            expect_pcode_data( viewer, segment, sizeof( temp ) );
            memcpy( &temp, segment->data, sizeof( temp ) );
            segment->data += sizeof( temp );
            num_args = temp;
         }
         else {
            expect_pcode_data( viewer, segment, sizeof( num_args ) );
            memcpy( &num_args, segment->data, sizeof( num_args ) );
            segment->data += sizeof( num_args );
         }
         int index = 0;
         if ( object->small_code ) {
            short temp = 0;
            expect_pcode_data( viewer, segment, sizeof( temp ) );
            memcpy( &temp, segment->data, sizeof( temp ) );
            segment->data += sizeof( temp );
            index = temp;
         }
         else {
            expect_pcode_data( viewer, segment, sizeof( index ) );
            memcpy( &index, segment->data, sizeof( index ) );
            segment->data += sizeof( index );
         }
         printf( " %d %d\n", num_args, index );
      }
      break;
   default:
      // For instructions that do not require any special handling of the
      // arguments, output arguments as integers.
      if ( g_pcodes[ segment->opcode ].num_args > 0 ) {
         for ( int i = 0; i < g_pcodes[ segment->opcode ].num_args; ++i ) {
            int arg = 0;
            expect_pcode_data( viewer, segment, sizeof( arg ) );
            memcpy( &arg, segment->data, sizeof( arg ) );
            segment->data += sizeof( arg );
            printf( " %d", arg );
         }
         printf( "\n" );
      }
      // No arguments.
      else {
         printf( "\n" );
      }
   }
}

static void show_sflg( struct viewer* viewer, struct chunk* chunk ) {
   int pos = 0;
   while ( pos < chunk->size ) {
      enum {
         FLAG_NET = 1,
         FLAG_CLIENTSIDE = 2
      };
      struct {
         short number;
         unsigned short flags;
      } entry;
      expect_chunk_data( viewer, chunk, chunk->data + pos, sizeof( entry ) );
      memcpy( &entry, chunk->data + pos, sizeof( entry ) );
      pos += sizeof( entry );
      printf( "script=%hd ", entry.number );
      unsigned short flags = entry.flags;
      printf( "flags=" );
      // Net flag.
      if ( flags & FLAG_NET ) {
         flags &= ~FLAG_NET;
         printf( "net(0x%x)", FLAG_NET );
         if ( flags != 0 ) {
            printf( "|" );
         }
      }
      // Clientside flag.
      if ( flags & FLAG_CLIENTSIDE ) {
         flags &= ~FLAG_CLIENTSIDE;
         printf( "clientside(0x%x)", FLAG_CLIENTSIDE );
         if ( flags != 0 ) {
            printf( "|" );
         }
      }
      // Unknown flags.
      if ( flags != 0 ) {
         printf( "unknown(0x%x)", flags );
      }
      printf( "\n" );
   }
}

static void show_svct( struct viewer* viewer, struct chunk* chunk ) {
   int pos = 0;
   while ( pos < chunk->size ) {
      struct {
         short number;
         short size;
      } entry;
      expect_chunk_data( viewer, chunk, chunk->data + pos, sizeof( entry ) ); 
      memcpy( &entry, chunk->data + pos, sizeof( entry ) );
      pos += sizeof( entry );
      printf( "script=%hd new-size=%hd\n", entry.number, entry.size );
   }
}

static void show_snam( struct viewer* viewer, struct chunk* chunk ) {
   const unsigned char* data = chunk->data;
   int total_names = 0;
   expect_chunk_data( viewer, chunk, data, sizeof( total_names ) );
   memcpy( &total_names, data, sizeof( total_names ) );
   data += sizeof( total_names );
   printf( "total-named-scripts=%d\n", total_names );
   for ( int i = 0; i < total_names; ++i ) {
      int offset = 0;
      expect_chunk_data( viewer, chunk, data, sizeof( offset ) );
      memcpy( &offset, data, sizeof( offset ) );
      data += sizeof( offset );
      enum { INITIAL_NAMEDSCRIPT_NUMBER = -1 };
      expect_chunk_offset_in_chunk( viewer, chunk, offset );
      printf( "script-number=%d script-name=\"%s\"\n",
         INITIAL_NAMEDSCRIPT_NUMBER - i,
         read_chunk_string( viewer, chunk, offset ) );
   }
}

static const char* read_chunk_string( struct viewer* viewer,
   struct chunk* chunk, int offset ) {
   // Make sure the string is NUL-terminated.
   if ( ! memchr( chunk->data + offset, '\0', chunk->size - offset ) ) {
      diag( viewer, DIAG_ERR,
         "a string at offset %d in %s chunk is not NUL-terminated", offset,
         chunk->name );
      bail( viewer );
   }
   return ( const char* ) ( chunk->data + offset );
}

static void show_strl_stre( struct viewer* viewer, struct chunk* chunk ) {
   const unsigned char* data = chunk->data;
   expect_chunk_data( viewer, chunk, data, sizeof( int ) );
   data += sizeof( int ); // Padding. Ignore it.
   int total_strings = 0;
   expect_chunk_data( viewer, chunk, data, sizeof( total_strings ) );
   memcpy( &total_strings, data, sizeof( total_strings ) );
   data += sizeof( total_strings );
   expect_chunk_data( viewer, chunk, data, sizeof( int ) );
   data += sizeof( int ); // Padding. Ignore it.
   printf( "table-size=%d\n", total_strings );
   for ( int i = 0; i < total_strings; ++i ) {
      int offset = 0;
      expect_chunk_data( viewer, chunk, data, sizeof( offset ) );
      memcpy( &offset, data, sizeof( offset ) );
      data += sizeof( offset );
      expect_chunk_offset_in_chunk( viewer, chunk, offset );
      show_string( i, offset, read_strl_stre_string( viewer, chunk, offset ),
         ( chunk->type == CHUNK_STRE ) );
   }
}

static const char* read_strl_stre_string( struct viewer* viewer,
   struct chunk* chunk, int offset ) {
   if ( ! is_strl_stre_string_nul_terminated( chunk, offset ) ) {
      diag( viewer, DIAG_ERR,
         "a string at offset %d in %s chunk is not NUL-terminated", offset,
         chunk->name );
      bail( viewer );
   }
   return ( const char* ) ( chunk->data + offset );
}

static bool is_strl_stre_string_nul_terminated( struct chunk* chunk,
   int offset ) {
   int i = offset;
   while ( i < chunk->size ) {
      char ch = ( char ) chunk->data[ i ];
      if ( chunk->type == CHUNK_STRE ) {
         ch = decode_ch( offset, i - offset, ch );
      }
      if ( ch == '\0' ) {
         return true;
      }
      ++i;
   }
   return false;
}

static char decode_ch( int string_offset, int offset, char ch ) {
   return ( ch ^ ( string_offset * 157135 + offset / 2 ) );
}

static void show_string( int index, int offset, const char* value,
   bool is_encoded ) {
   printf( "[%d] offset=%d", index, offset );
   printf( " " );
   printf( "\"" );
   int i = 0;
   while ( true ) {
      char ch = value[ i ];
      if ( is_encoded ) {
         ch = decode_ch( offset, i, ch );
      }
      if ( ch == '\0' ) {
         break;
      }
      // Make the output of some characters more pretty.
      if ( ch == '"' ) {
         printf( "\\\"" );
      }
      else if ( ch == '\r' ) {
         printf( "\\r" );
      }
      else if ( ch == '\n' ) {
         printf( "\\n" );
      }
      else {
         printf( "%c", ch );
      }
      ++i;
   }
   printf( "\"" );
   printf( "\n" );
}

static void show_sary_fary( struct viewer* viewer, struct chunk* chunk ) {
   const unsigned char* data = chunk->data;
   short index = 0;
   expect_chunk_data( viewer, chunk, data, sizeof( index ) );
   memcpy( &index, data, sizeof( index ) );
   data += sizeof( index );
   int size = 0; // Size of a script array.
   int total_arrays = ( chunk->size - sizeof( index ) ) / sizeof( size );
   printf( "%s=%d total-script-arrays=%d\n",
      ( chunk->type == CHUNK_FARY ) ? "function" : "script",
      index, total_arrays );
   for ( int i = 0; i < total_arrays; ++i ) {
      expect_chunk_data( viewer, chunk, data, sizeof( size ) );
      memcpy( &size, data, sizeof( size ) );
      data += sizeof( size );
      printf( "array-index=%d array-size=%d\n", i, size );
   }
}

static void show_alib( struct chunk* chunk ) {
   printf( "library=yes\n" );
}

static bool view_chunk( struct viewer* viewer, struct object* object,
   const char* name ) {
   int type = get_chunk_type( name );
   if ( type == CHUNK_UNKNOWN ) {
      printf( "error: unsupported chunk: %s\n", name );
      return false;
   }
   struct chunk chunk;
   struct chunk_reader reader;
   init_chunk_reader( &reader, object );
   bool found = false;
   while ( read_chunk( viewer, &reader, &chunk ) ) {
      if ( chunk.type == type ) {
         show_chunk( viewer, object, &chunk, true );
         found = true;
      }
   }
   if ( found ) {
      return true;
   }
   else {
      printf( "error: `%s` chunk not found\n", name );
      return false;
   }
}

static void init_chunk_reader( struct chunk_reader* reader,
   struct object* object ) {
   reader->object = object;
   reader->end_pos = ( object->indirect_format ) ?
      object->real_header_offset : object->size;
   reader->pos = object->chunk_offset;
}

static bool read_chunk( struct viewer* viewer, struct chunk_reader* reader,
   struct chunk* chunk ) {
   if ( reader->end_pos - reader->pos >= sizeof( struct chunk_header ) ) {
      init_chunk( viewer, reader->object, reader->pos, chunk );
      reader->pos += sizeof( struct chunk_header ) + chunk->size;
      return true;
   }
   else {
      return false;
   }
}

static void init_chunk( struct viewer* viewer, struct object* object,
   int offset, struct chunk* chunk ) {
   const unsigned char* data = object->data + offset;
   struct chunk_header header;
   expect_data( viewer, object, data, sizeof( header ) );
   memcpy( &header, data, sizeof( header ) );
   data += sizeof( header );
   memcpy( chunk->name, header.name, sizeof( header.name ) );
   chunk->name[ sizeof( header.name ) ] = '\0';
   chunk->size = header.size;
   chunk->data = data;
   chunk->type = get_chunk_type( chunk->name );
   expect_data( viewer, object, data, chunk->size );
}

static int get_chunk_type( const char* name ) {
   char buff[ 5 ];
   memcpy( buff, name, 4 );
   for ( int i = 0; i < 4; ++i ) {
      buff[ i ] = toupper( buff[ i ] );
   }
   buff[ 4 ] = 0;
   static const struct {
      const char* name;
      int type;
   } supported[] = {
      { "ARAY", CHUNK_ARAY },
      { "AINI", CHUNK_AINI },
      { "AIMP", CHUNK_AIMP },
      { "ASTR", CHUNK_ASTR },
      { "MSTR", CHUNK_MSTR },
      { "ATAG", CHUNK_ATAG },
      { "LOAD", CHUNK_LOAD },
      { "FUNC", CHUNK_FUNC },
      { "FNAM", CHUNK_FNAM },
      { "MINI", CHUNK_MINI },
      { "MIMP", CHUNK_MIMP },
      { "MEXP", CHUNK_MEXP },
      { "SPTR", CHUNK_SPTR },
      { "SFLG", CHUNK_SFLG },
      { "SVCT", CHUNK_SVCT },
      { "SNAM", CHUNK_SNAM },
      { "STRL", CHUNK_STRL },
      { "STRE", CHUNK_STRE },
      { "SARY", CHUNK_SARY },
      { "FARY", CHUNK_FARY },
      { "ALIB", CHUNK_ALIB },
      { "", CHUNK_UNKNOWN }
   };
   int i = 0;
   while ( supported[ i ].type != CHUNK_UNKNOWN &&
      strcmp( buff, supported[ i ].name ) != 0 ) {
      ++i;
   }
   return supported[ i ].type;
}

static bool find_chunk( struct viewer* viewer, struct object* object,
   const char* name, struct chunk* chunk ) {
   struct chunk_reader reader;
   init_chunk_reader( &reader, object );
   while ( read_chunk( viewer, &reader, chunk ) ) {
      if ( strcmp( name, chunk->name ) == 0 ) {
         return true;
      }
   }
   return false;
}

static void show_object( struct viewer* viewer, struct object* object ) {
   switch ( object->format ) {
   case FORMAT_BIG_E:
   case FORMAT_LITTLE_E:
      show_all_chunks( viewer, object );
      break;
   default:
      break;
   }
   if ( script_directory_present( object ) ) {
      show_script_directory( viewer, object );
      show_string_directory( viewer, object );
   }
}

static void show_all_chunks( struct viewer* viewer, struct object* object ) {
   struct chunk chunk;
   struct chunk_reader reader;
   init_chunk_reader( &reader, object );
   while ( read_chunk( viewer, &reader, &chunk ) ) {
      show_chunk( viewer, object, &chunk, true );
   }
}

static void show_script_directory( struct viewer* viewer,
   struct object* object ) {
   printf( "== script directory (offset=%d)\n", object->directory_offset );
   const unsigned char* data = object->data + object->directory_offset;
   int total_scripts = 0;
   expect_data( viewer, object, data, sizeof( total_scripts ) );
   memcpy( &total_scripts, data, sizeof( total_scripts ) );
   data += sizeof( total_scripts );
   printf( "total-scripts=%d\n", total_scripts );
   for ( int i = 0; i < total_scripts; ++i ) {
      struct acs0_script_entry entry;
      expect_data( viewer, object, data, sizeof( entry ) );
      memcpy( &entry, data, sizeof( entry ) );
      data += sizeof( entry );
      int number = entry.number % 1000;
      int type = entry.number / 1000;
      printf( "script=%d ", number );
      const char* name = get_script_type_name( type );
      if ( name ) {
         printf( "type=%s ", name );
      }
      else {
         printf( "type=unknown:%d ", type );
      }
      printf( "params=%d offset=%d\n", entry.num_param, entry.offset );
      if ( offset_in_object_file( object, entry.offset ) ) {
         show_pcode( viewer, object, entry.offset,
            calc_code_size( viewer, object, entry.offset ) );
      }
      else {
         diag( viewer, DIAG_WARN,
            "offset (%d) points outside the object file, so the script code "
            "will not be shown", entry.offset );  
      }
   }
}

static void show_string_directory( struct viewer* viewer,
   struct object* object ) {
   printf( "== string directory (offset=%d)\n", object->string_offset );
   const unsigned char* data = object->data + object->string_offset;
   int total_strings = 0;
   expect_data( viewer, object, data, sizeof( total_strings ) );
   memcpy( &total_strings, data, sizeof( total_strings ) );
   data += sizeof( total_strings );
   printf( "total-strings=%d\n", total_strings );
   for ( int i = 0; i < total_strings; ++i ) {
      int offset = 0;
      expect_data( viewer, object, data, sizeof( offset ) );
      memcpy( &offset, data, sizeof( offset ) );
      data += sizeof( offset );
      show_string( i, offset, ( const char* ) ( object->data + offset ),
         false );
   }
}

static void diag( struct viewer* viewer, int flags, const char* format, ... ) {
   // Message type qualifier.
   if ( flags & DIAG_INTERNAL ) {
      printf( "internal " );
   }
   // Message type.
   if ( flags & DIAG_ERR ) {
      printf( "error: " );
   }
   else if ( flags & DIAG_WARN ) {
      printf( "warning: " );
   }
   else if ( flags & DIAG_NOTE ) {
      printf( "note: " );
   }
   // Message.
   va_list args;
   va_start( args, format );
   vprintf( format, args );
   va_end( args );
   printf( "\n" );
}

static void bail( struct viewer* viewer ) {
   longjmp( viewer->bail, 1 );
}
