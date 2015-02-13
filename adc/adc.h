#pragma once

#include <assert.h>
#ifndef _WIN32
#include <dirent.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "win_dirent.h"
#endif

#include "stb_sb.h"

// ============================================================
// ad-C
// ============================================================

void adc_clear_file(const char* fname);

// NOTE: this function dumbly replaces text. It doesn't care about grammar.
// e.g. it will replace tokens inside of strings.
void adc_expand(
        const char* output_path,
        const char* tmpl_path,
        ...);

// Searches for *.c *.h *.cpp and *.cc files in directory_path, parses them,
// and generates type info at output_path
void adc_type_info(
        const char* output_path,
        const char* directory_path);

// ============================================================

static const size_t bytes_in_fd(FILE* fd)
{
    fpos_t fd_pos;
    fgetpos(fd, &fd_pos);
    fseek(fd, 0, SEEK_END);
    size_t len = (size_t)ftell(fd);
    fsetpos(fd, &fd_pos);
    return len;
}

static const char* slurp_file(const char* path, size_t *out_size)
{
    FILE* fd = fopen(path, "r");
    assert(fd);
    if (!fd)
    {
        fprintf(stderr, "ERROR: couldn't slurp %s\n", path);
        return NULL;
    }
    const size_t len = bytes_in_fd(fd);
    const char* contents = malloc(len);
    fread((void*)contents, len, 1, fd);
    fclose(fd);
    *out_size = len;
    return contents;
}

void adc_clear_file(const char* path)
{
    FILE* fd = fopen(path, "w+");
    assert(fd);
    static const char message[] = "//File generated from template by ad-C.\n\n";
    fwrite(message, sizeof(char), sizeof(message) - 1, fd);
    fclose(fd);
}

typedef struct
{
    char* str;
    size_t len;
} TemplateToken;

typedef struct
{
    TemplateToken name;
    TemplateToken substitution;
} Binding;

void adc_expand(
        const char* result_path,
        const char* tmpl_path,
        const int num_bindings,
        ...)
{
    enum
    {
        LEX_NOTHING,
        LEX_DOLLAR,
        LEX_BEGIN,
        LEX_INSIDE,
    };

    // Get bindings

    Binding* bindings = NULL;

    va_list ap;

    va_start(ap, num_bindings);
    for (int i = 0; i < num_bindings; ++i)
    {
        char* name = va_arg(ap, char*);
        char* subst = va_arg(ap, char*);

        TemplateToken tk_name = { name, strlen(name) };
        TemplateToken tk_subst = { subst, strlen(subst) };

        Binding binding = { tk_name, tk_subst };
        sb_push(bindings, binding);
    }
    va_end(ap);

    FILE* out_fd = fopen(result_path, "a");
    assert(out_fd);
    size_t data_size = 0;
    const char* in_data = slurp_file(tmpl_path, &data_size);
    char* out_data = NULL;

    size_t path_len = strlen(tmpl_path);
    sb_push(out_data, '/'); sb_push(out_data, '/');
    for (int i = 0; i < path_len; ++i)
    {
        sb_push(out_data, tmpl_path[i]);
    }
    sb_push(out_data, '\n');
    sb_push(out_data, '\n');

    TemplateToken* tokens = NULL;

    int lexer_state = LEX_NOTHING;

    char prev = 0;
    char* name = NULL;
    int name_len = 0;
    for (int i = 0; i < data_size; ++i)
    {
        char c = in_data[i];
        // Handle transitions
        {
            if ( lexer_state == LEX_NOTHING && c != '$' )
            {
                sb_push(out_data, c);
            }
            if ( lexer_state == LEX_NOTHING && c == '$' )
            {
                lexer_state = LEX_DOLLAR;
            }
            else if ( lexer_state == LEX_DOLLAR && c == '<' )
            {
                lexer_state = LEX_INSIDE;
            }
            else if (lexer_state == LEX_INSIDE && c != '>')
            {
                // add char to name
                sb_push(name, c);
                ++name_len;
            }
            else if (lexer_state == LEX_INSIDE && c == '>')
            {
                // add token
                sb_push(name, '\0');
                TemplateToken token;
                token.str = name;
                token.len = name_len;
                name = NULL;
                sb_push(tokens, token);
                name_len = 0;
                lexer_state = LEX_NOTHING;

                // Do stupid search on args to get matching subst.
                for (int i = 0; i < sb_count(bindings); ++i)
                {
                    const Binding binding = bindings[i];
                    if (!strcmp(binding.name.str, token.str))
                    {
                        for (int j = 0; j < binding.substitution.len; ++j)
                        {
                            char c = binding.substitution.str[j];
                            sb_push(out_data, c);
                        }
                        break;
                    }
                }
            }
        }
        prev = c;
    }

    sb_push(out_data, '\n');

    fwrite(out_data, sizeof(char), sb_count(out_data), out_fd);
    fclose(out_fd);
}

// Note:
//  Parser should output defines of the form
//  #define ADCTYPE_FILE_filename_SCOPE_scope_NAME_name
//  `scope` can be 'global' or the name of a top level function/struct/enum
//      e.g. my_file.c has a global int variable named foo
//      therefore we have:
//      #define ADCTYPE_FILE_my_file_SCOPE_global_NAME_foo int


static int is_whitespace(char c)
{
    if (
            c == ' '  |
            c == '\v' |
            c == '\t' |
            c == '\r'
       )
    {
        return 1;
    }
    return 0;
}

static int is_ident_char(char c)
{
    char idents[]=
        {
            'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
            'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 'A', 'B', 'C', 'D', 'E', 'F',
            'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
            'W', 'X', 'Y', 'Z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '_',
            '.', // We treat accessors as part of the identifier
            ':', // special support for light c++
        };
    for (int i = 0; i < sizeof(idents); ++i)
    {
        if (c == idents[i])
            return 1;
    }
    return 0;
}

/* static int is_operator(char c) */
/* { */
/*     return ( */
/*             c == '*' | */
/*             c == '/' | */
/*             c == '+' | */
/*             c == '-' | */
/*             c == '%' | */
/*             c == '=' | */
/*             c == '^' | */
/*             c == '|' | */
/*             c == '&' */
/*            ); */
/* } */

enum
{
    LEX_BEGIN_LINE,
    LEX_WAIT_FOR_NEWLINE,
    LEX_ONE_SLASH,
    LEX_RECEIVING,
    LEX_INSIDE_STRING,
    LEX_INSIDE_CHAR,
    LEX_ASSIGN,
};


#define BUFFERSIZE (10 * 1024)

static void process_file(FILE* out_fd, const char** known_types, int count_types, const char* fname)
{
    size_t file_size;
    const char* file_contents = slurp_file(fname, &file_size);
    int lex_state = LEX_BEGIN_LINE;

    char* tokenstack[BUFFERSIZE];
    int tokencount = 0;

    if (file_contents)
    {

        char** idents = 0;
        char* curtok = 0;
        char prev_c = 0;

        for (int i = 0; i < file_size; ++i)
        {
            char c = file_contents[i];
            ///////////
            // HANDLE STUFF THAT SHOULD BE IGNORED
            // ////////
            if ( lex_state == LEX_WAIT_FOR_NEWLINE && c == '\n' )
            {
                lex_state = LEX_BEGIN_LINE;
            }
            else if ( lex_state == LEX_BEGIN_LINE && c == '#' )
            {
                lex_state = LEX_WAIT_FOR_NEWLINE;
            }
            else if ( lex_state == LEX_BEGIN_LINE && is_whitespace(c) )
            {
                lex_state = LEX_RECEIVING;
            }
            else if ( (lex_state == LEX_RECEIVING || lex_state == LEX_BEGIN_LINE) && c == '/')
            {
                lex_state = LEX_ONE_SLASH;
            }
            else if ( lex_state == LEX_ONE_SLASH && c == '/' )
            {
                lex_state = LEX_WAIT_FOR_NEWLINE;
            }
            else if (lex_state == LEX_BEGIN_LINE && c == '\n')
            {
                lex_state = LEX_BEGIN_LINE;
            }
            ////////
            // Handle strings
            ////////
            else if ( (lex_state == LEX_RECEIVING || lex_state == LEX_BEGIN_LINE) && c == '\"' )
            {
                lex_state = LEX_INSIDE_STRING;
            }
            else if ( lex_state == LEX_INSIDE_STRING && c == '\"' && prev_c != '\\' )
            {
                lex_state = LEX_RECEIVING;
            }
            else if ( (lex_state == LEX_RECEIVING || lex_state == LEX_BEGIN_LINE) && c == '\'' )
            {
                lex_state = LEX_INSIDE_CHAR;
            }
            else if ( lex_state == LEX_INSIDE_CHAR && c == '\''  && prev_c != '\\' )
            {
                lex_state = LEX_RECEIVING;
            }
            ///////
            // Asignment.
            //////
            else if ( (lex_state == LEX_RECEIVING || lex_state == LEX_BEGIN_LINE) && c == '=' )
            {
                lex_state = LEX_ASSIGN;  // Need to differentiate between assignmt and equals operator.
            }
            else if ( lex_state == LEX_ASSIGN && c != '=')
            {
                if (tokencount > 2)
                {
                    char* name = tokenstack[--tokencount];
                    char** type_decl = 0;
                    char* token = tokenstack[--tokencount];
                    int count_decl = 0;
                    int found_invalid = 0;
                    while (strcmp(token, ";"))
                    {
                        found_invalid = 1;
                        // Find token in known valid type declaration names
                        for (int i = 0; i < count_types; ++i)
                        {
                            if (!strcmp(known_types[i], token))
                            {
                                found_invalid = 0;
                            }

                        }
                        // break if not in known type
                        if (found_invalid) break;
                        sb_push(type_decl, token);
                        token = tokenstack[--tokencount];
                        count_decl++;
                    }
                    if (count_decl)
                    {
                        printf("Assignment: %s : ", name);
                        for (int i = 0; i < count_decl; ++i)
                        {
                            printf("%s ", type_decl[i]);
                        }
                        puts(".");
                    }
                }
                lex_state = LEX_RECEIVING;
            }
            ////////
            // parse token
            ///////
            else if ( lex_state == LEX_RECEIVING || lex_state == LEX_BEGIN_LINE )
            {
                lex_state = LEX_RECEIVING;
                if (is_ident_char(c))
                {
                    sb_push(curtok, c);
                }
                else  // Finish token
                {
                    // Handle empty line.
                    if (!curtok)
                    {
                        continue;
                    }
                    sb_push(curtok, '\0');
                    puts(curtok);
                    // Do something with token
                    tokenstack[tokencount++] = curtok;
                    curtok = 0;  // Reset token
                    if (c == ';')
                    {
                        tokenstack[tokencount++] = ";";
                        puts(";");
                    }
                }
            }
            prev_c = c;
        }
    }
    else
    {
        fprintf(stderr, "Could not open file for processing, %s\n", fname);
    }

}

void adc_type_info(
        const char* output_path,
        const char* directory_path)
{
    assert(output_path);
    assert(directory_path);


    // Init known C types
    char** known_types = 0;
    int count_types = 0;
    {
        char* init_types[] =
        {
            "const",
            "unsigned", "char", "short", "int", "long", "float", "double",
            "uint8_t", "uint16_t", "uint32_t", "uint64_t",
            "int8_t", "int16_t", "int32_t", "int64_t",
        };
        count_types = (sizeof(init_types) / sizeof(char*));
        for (int i = 0; i < count_types; ++i)
        {
            sb_push(known_types, init_types[i]);
        }
    }

    FILE* fd = fopen(output_path, "w+");
    if (!fd)
    {
        fprintf(stderr, "Could not open file %s for writing.\n", output_path);
        exit(-1);
    }
    // Traverse directory, fill filename array
    DIR* dirstack[256] = { 0 };
    int dircount = 0;

    DIR* dir = opendir(directory_path);
    dirstack[dircount++] = dir;
    while (dircount)
    {
        DIR* dir = dirstack[--dircount];
        struct dirent* ent = readdir(dir);
        char fname[1024];
        while(ent)
        {
            fname[0] = '\0';
            strcat(fname, dir->name);
            fname[strlen(fname) - 1] = '\0'; // Remove * character
            strcat(fname, ent->d_name);

            const size_t fname_len = strlen(fname);
            if (fname[fname_len - 1] != '.')
            {
                DIR* sub_dir = opendir(fname);
                if (sub_dir)
                {
                    dirstack[dircount++] = sub_dir;
                }
                else  // Is a file.
                {
                    printf("%s\n", fname);
                    char* acceptable_exts[] = {"cc", "cpp", "c", "h", "hh", "hpp"};

                    int accepted = 0;
                    { // Check that file extension is OK
                        char* fname_ext = 0;
                        for(size_t i = fname_len; i > 0; --i)
                        {
                            int c = fname[i];
                            if (c == '.')
                            {
                                fname_ext = fname + i + 1;
                                break;
                            }
                        }
                        for (int i = 0; i < sizeof(acceptable_exts) / sizeof(char*); ++i)
                        {
                            if (!strcmp(fname_ext, acceptable_exts[i]))
                            {
                                accepted = 1;
                                break;
                            }
                        }
                    }
                    // Process all C files.
                    if (accepted)
                    {
                        process_file(fd, known_types, count_types, fname);
                    }
                }
            }
            ent = readdir(dir);
        }

        closedir(dir);
    }
}
