/*
   Copyright (C) 2000-2006 Silicon Graphics, Inc.  All Rights Reserved.
   Portions Copyright (C) 2007-2013 David Anderson. All Rights Reserved.
   Portions Copyright (C) 2010-2012 SN Systems Ltd. All Rights Reserved.
   Portions Copyright (C) 2015-2015 Google, Inc. All Rights Reserved.

   This program is free software; you can redistribute it and/or modify it
   under the terms of version 2.1 of the GNU Lesser General Public License
   as published by the Free Software Foundation.

   This program is distributed in the hope that it would be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

   Further, this software is distributed without any warranty that it is
   free of the rightful claim of any third person regarding infringement
   or the like.  Any license provided herein, whether implied or
   otherwise, applies only to this software file.  Patent licenses, if
   any, provided herein do not apply to combinations of this program with
   other software, or any other product whatsoever.

   You should have received a copy of the GNU Lesser General Public
   License along with this program; if not, write the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston MA 02110-1301,
   USA.

*/

/*  This is #included twice. Once for
    libdwarf callers and one for dwarfdump which prints
    the internals.

    This way we have just one blob of code that reads
    the table operations.  */

#define TRUE 1
#define FALSE 0

static unsigned char
dwarf_standard_opcode_operand_count[STANDARD_OPERAND_COUNT_TWO_LEVEL] = {
    /* DWARF2 */
    0,
    1, 1, 1, 1,
    0, 0, 0,
    1,
    /* Following are new for DWARF3. */
    0, 0, 1,
    /* Experimental opcodes. */
    1, 2, 0,
};

/* We have a normal standard opcode base, but
   an arm compiler emitted a non-standard table!
   This could lead to problems...
   ARM C/C++ Compiler, RVCT4.0 [Build 4
   00] seems to get the table wrong .  */
static unsigned char
dwarf_arm_standard_opcode_operand_count[STANDARD_OPERAND_COUNT_DWARF3] = {
    /* DWARF2 */
    0,
    1, 1, 1, 1,
    0, 0, 0,
    0,  /* <<< --- this is wrong */
    /* Following are new for DWARF3. */
    0, 0, 1
};

/* Rather like memcmp but identifies which value pair
    mismatches (the return value is non-zero if mismatch,
    zero if match)..
    mismatch_entry returns the table index that mismatches.
    tabval returns the table byte value.
    lineval returns the value from the line table header.  */
static int
operandmismatch(unsigned char * table,unsigned table_length,
    unsigned char *linetable,
    unsigned check_count,
    unsigned * mismatch_entry, unsigned * tabval,unsigned *lineval)
{
    unsigned i = 0;

    /* check_count better be <= table_length */
    for (i = 0; i<check_count; ++i) {
        if (i > table_length) {
            *mismatch_entry = i;
            *lineval = linetable[i];
            *tabval = 0; /* No entry present. */
            /* A kind of mismatch */
            return  TRUE;
        }
        if (table[i] == linetable[i]) {
            continue;
        }
        *mismatch_entry = i;
        *tabval = table[i];
        *lineval = linetable[i];
        return  TRUE;
    }
    /* Matches. */
    return FALSE;
}


/* Common line table header reading code.
   Returns DW_DLV_OK, DW_DLV_ERROR.
   DW_DLV_NO_ENTRY cannot be returned, but callers should
   assume it is possible.

   The line_context area must be initialized properly before calling this.

   Has the side effect of allocating arrays which
   must be freed (see the Line_Table_Context which
   holds the pointers to space we allocate here).

   bogus_bytes_ptr and bogus_bytes are output values which
   let a print-program notify the user of some surprising bytes
   after a line table header and before the line table instructions.
   These can be ignored unless one is printing.
   And are ignored if NULL passed as the pointer.

   err_count_out may be NULL, in which case we
   make no attempt to count checking-type errors.
   Checking-type errors do not stop us, we just report them.

   See dw-linetableheader.txt for the ordering of text fields
   across the various dwarf versions. The code
   follows this ordering closely.

   Some of the arguments remaining are in line_context
   so can be deleted from the
   argument list (after a close look for correctness).
*/
static int
_dwarf_read_line_table_header(Dwarf_Debug dbg,
    Dwarf_CU_Context cu_context,
    Dwarf_Small * section_start,
    Dwarf_Small * data_start,
    Dwarf_Unsigned section_length,
    Dwarf_Small ** updated_data_start_out,
    Dwarf_Line_Context  line_context,
    Dwarf_Small ** bogus_bytes_ptr,
    Dwarf_Unsigned *bogus_bytes,
    Dwarf_Error * err,
    int *err_count_out)
{
    Dwarf_Small *line_ptr = data_start;
    Dwarf_Small *starting_line_ptr = data_start;
    Dwarf_Unsigned total_length = 0;
    int local_length_size = 0;
    int local_extension_size = 0;
    Dwarf_Unsigned prologue_length = 0;
    Dwarf_Half version = 0;
    Dwarf_Small *section_end = section_start + section_length;
    Dwarf_Small *line_ptr_end = 0;
    Dwarf_Small *lp_begin = 0;
    int res = 0;

    if (bogus_bytes_ptr) *bogus_bytes_ptr = 0;
    if (bogus_bytes) *bogus_bytes= 0;

    line_context->lc_line_ptr_start = starting_line_ptr;
    /* READ_AREA_LENGTH updates line_ptr for consumed bytes */
    READ_AREA_LENGTH_CK(dbg, total_length, Dwarf_Unsigned,
        line_ptr, local_length_size, local_extension_size,
        err, section_length,section_end);

    line_ptr_end = line_ptr + total_length;
    line_context->lc_line_ptr_end = line_ptr_end;
    line_context->lc_length_field_length = local_length_size +
        local_extension_size;
    line_context->lc_section_offset = starting_line_ptr -
        dbg->de_debug_line.dss_data;
    /*  ASSERT: line_context->lc_length_field_length == line_ptr
        -line_context->lc_line_ptr_start; */
    if (line_ptr_end > section_end) {
        _dwarf_error(dbg, err, DW_DLE_DEBUG_LINE_LENGTH_BAD);
        return (DW_DLV_ERROR);
    }
    line_context->lc_total_length = total_length;

    READ_UNALIGNED_CK(dbg, version, Dwarf_Half,
        line_ptr, sizeof(Dwarf_Half),
        err,line_ptr_end);
    line_context->lc_version_number = version;
    line_ptr += sizeof(Dwarf_Half);
    if (version != DW_LINE_VERSION2 &&
        version != DW_LINE_VERSION3 &&
        version != DW_LINE_VERSION4 &&
        version != DW_LINE_VERSION5 &&
        version != EXPERIMENTAL_LINE_TABLES_VERSION) {
        _dwarf_error(dbg, err, DW_DLE_VERSION_STAMP_ERROR);
        return (DW_DLV_ERROR);
    }
    if (version == DW_LINE_VERSION5) {
        line_context->lc_address_size = *(unsigned char *) line_ptr;
        line_ptr = line_ptr + sizeof(Dwarf_Small);
        line_context->lc_segment_selector_size =
            *(unsigned char *) line_ptr;
        line_ptr = line_ptr + sizeof(Dwarf_Small);
    } else {
        line_context->lc_address_size = cu_context->cc_address_size;
        line_context->lc_segment_selector_size =
            cu_context->cc_segment_selector_size;
    }

    READ_UNALIGNED_CK(dbg, prologue_length, Dwarf_Unsigned,
        line_ptr, local_length_size,
        err,line_ptr_end);
    line_context->lc_prologue_length = prologue_length;
    line_ptr += local_length_size;
    line_context->lc_line_prologue_start = line_ptr;
    if(line_ptr >= line_ptr_end) {
        _dwarf_error(dbg, err, DW_DLE_LINE_OFFSET_BAD);
        return DW_DLV_ERROR;
    }


    line_context->lc_minimum_instruction_length =
        *(unsigned char *) line_ptr;
    line_ptr = line_ptr + sizeof(Dwarf_Small);

    if (version == DW_LINE_VERSION4 ||
        version == DW_LINE_VERSION5 ||
        version == EXPERIMENTAL_LINE_TABLES_VERSION) {
        line_context->lc_maximum_ops_per_instruction =
            *(unsigned char *) line_ptr;
        line_ptr = line_ptr + sizeof(Dwarf_Small);
    }
    line_context->lc_default_is_stmt = *(unsigned char *) line_ptr;
    line_ptr = line_ptr + sizeof(Dwarf_Small);

    line_context->lc_line_base = *(signed char *) line_ptr;
    line_ptr = line_ptr + sizeof(Dwarf_Sbyte);
    if(line_ptr >= line_ptr_end) {
        _dwarf_error(dbg, err, DW_DLE_LINE_OFFSET_BAD);
        return DW_DLV_ERROR;
    }

    line_context->lc_line_range = *(unsigned char *) line_ptr;
    if (!line_context->lc_line_range) {
        _dwarf_error(dbg, err, DW_DLE_DEBUG_LINE_RANGE_ZERO);
        return DW_DLV_ERROR;
    }
    line_ptr = line_ptr + sizeof(Dwarf_Small);
    if(line_ptr >= line_ptr_end) {
        _dwarf_error(dbg, err, DW_DLE_LINE_OFFSET_BAD);
        return DW_DLV_ERROR;
    }
    line_context->lc_opcode_base = *(unsigned char *) line_ptr;
    line_ptr = line_ptr + sizeof(Dwarf_Small);
    /*  Set up the array of standard opcode lengths. */
    /*  We think this works ok even for cross-endian processing of
        objects.  It might be wrong, we might need to specially process
        the array of ubyte into host order.  */
    line_context->lc_opcode_length_table = line_ptr;

    /*  lc_opcode_base is one greater than the size of the array. */
    line_ptr += line_context->lc_opcode_base - 1;
    line_context->lc_std_op_count = line_context->lc_opcode_base -1;
    if(line_ptr >= line_ptr_end) {
        _dwarf_error(dbg, err, DW_DLE_LINE_OFFSET_BAD);
        return DW_DLV_ERROR;
    }
    {
        /*  Determine (as best we can) whether the
            lc_opcode_length_table holds 9 or 12 standard-conforming
            entries.  gcc4 upped to DWARF3's 12 without updating the
            version number.
            EXPERIMENTAL_LINE_TABLES_VERSION upped to 15.  */
        unsigned check_count = line_context->lc_std_op_count;
        unsigned tab_count = sizeof(dwarf_standard_opcode_operand_count);

        int operand_ck_fail = true;
        if (line_context->lc_std_op_count > tab_count) {
            _dwarf_print_header_issue(dbg,
                "Too many standard operands in linetable header: ",
                data_start,
                line_context->lc_std_op_count,
                0,0,0,
                err_count_out);
            check_count = tab_count;
        }
        {
            unsigned entrynum = 0;
            unsigned tabv     = 0;
            unsigned linetabv     = 0;

            int mismatch = operandmismatch(
                dwarf_standard_opcode_operand_count,
                tab_count,
                line_context->lc_opcode_length_table,
                check_count,&entrynum,&tabv,&linetabv);
            if (mismatch) {
                if (err_count_out) {
                    _dwarf_print_header_issue(dbg,
                        "standard-operands did not match, checked",
                        data_start,
                        check_count,
                        entrynum,tabv,linetabv,err_count_out);
                }
                if (check_count >
                    sizeof(dwarf_arm_standard_opcode_operand_count)) {
                    check_count =
                        sizeof(dwarf_arm_standard_opcode_operand_count);
                }
                mismatch = operandmismatch(
                    dwarf_arm_standard_opcode_operand_count,
                    sizeof(dwarf_arm_standard_opcode_operand_count),
                    line_context->lc_opcode_length_table,
                    check_count,&entrynum,&tabv,&linetabv);
                if (!mismatch && err_count_out) {
                    _dwarf_print_header_issue(dbg,
                        "arm (incorrect) operands in use: ",
                        data_start,
                        check_count,
                        entrynum,tabv,linetabv,err_count_out);
                }
            }
            if (!mismatch) {
                if (version == 2) {
                    if (line_context->lc_std_op_count ==
                        STANDARD_OPERAND_COUNT_DWARF3) {
                        _dwarf_print_header_issue(dbg,
                            "standard DWARF3 operands matched,"
                            " but is DWARF2 linetable: count",
                            data_start,
                            check_count,
                            0,0,0, err_count_out);
                    }
                }
                operand_ck_fail = false;
            }
        }
        if (operand_ck_fail) {
            /* Here we are not sure what the lc_std_op_count is. */
            _dwarf_error(dbg, err, DW_DLE_LINE_NUM_OPERANDS_BAD);
            return (DW_DLV_ERROR);
        }
    }
    /*  At this point we no longer need to check operand counts. */
    if(line_ptr >= line_ptr_end) {
        _dwarf_error(dbg, err, DW_DLE_LINE_OFFSET_BAD);
        return DW_DLV_ERROR;
    }

    if (version < DW_LINE_VERSION5){
        Dwarf_Unsigned directories_count = 0;
        Dwarf_Unsigned directories_malloc = 5;
        line_context->lc_include_directories = malloc(sizeof(Dwarf_Small *) *
            directories_malloc);
        if (line_context->lc_include_directories == NULL) {
            _dwarf_error(dbg, err, DW_DLE_ALLOC_FAIL);
            return (DW_DLV_ERROR);
        }
        memset(line_context->lc_include_directories, 0,
            sizeof(Dwarf_Small *) * directories_malloc);

        if (line_ptr >= line_ptr_end) {
            _dwarf_error(dbg, err, DW_DLE_LINE_NUMBER_HEADER_ERROR);
            return (DW_DLV_ERROR);
        }
        while ((*(char *) line_ptr) != '\0') {
            if (directories_count >= directories_malloc) {
                Dwarf_Unsigned expand = 2 * directories_malloc;
                Dwarf_Unsigned bytesalloc = sizeof(Dwarf_Small *) * expand;
                Dwarf_Small **newdirs =
                    realloc(line_context->lc_include_directories,
                        bytesalloc);

                if (!newdirs) {
                    _dwarf_error(dbg, err, DW_DLE_ALLOC_FAIL);
                    return (DW_DLV_ERROR);
                }
                /* Doubled size, zero out second half. */
                memset(newdirs + directories_malloc, 0,
                    sizeof(Dwarf_Small *) * directories_malloc);
                directories_malloc = expand;
                line_context->lc_include_directories = newdirs;
            }
            line_context->lc_include_directories[directories_count] =
                line_ptr;
            res = _dwarf_check_string_valid(dbg,
                data_start,line_ptr,line_ptr_end,err);
            if (res != DW_DLV_OK) {
                return res;
            }
            line_ptr = line_ptr + strlen((char *) line_ptr) + 1;
            directories_count++;
            if (line_ptr >= line_ptr_end) {
                _dwarf_error(dbg, err, DW_DLE_LINE_NUMBER_HEADER_ERROR);
                return (DW_DLV_ERROR);
            }
        }
        line_ptr++;
        line_context->lc_include_directories_count = directories_count;
    } else if (version == EXPERIMENTAL_LINE_TABLES_VERSION) {
        /* Empty old style dir entry list. */
        line_ptr++;
    } else {
        /* No old style directory entries. */
    }
    if(line_ptr > line_ptr_end) {
        _dwarf_error(dbg, err, DW_DLE_LINE_OFFSET_BAD);
        return DW_DLV_ERROR;
    }
    if (version < DW_LINE_VERSION5) {
        if (line_ptr >= line_ptr_end) {
            _dwarf_error(dbg, err, DW_DLE_LINE_NUMBER_HEADER_ERROR);
            return (DW_DLV_ERROR);
        }
        while (*(char *) line_ptr != '\0') {
            Dwarf_Unsigned utmp;
            Dwarf_Unsigned dir_index = 0;
            Dwarf_Unsigned lastmod = 0;
            Dwarf_Unsigned file_length = 0;
            int resl = 0;
            Dwarf_File_Entry currfile  = 0;

            currfile = (Dwarf_File_Entry)
                malloc(sizeof(struct Dwarf_File_Entry_s));
            if (currfile == NULL) {
                _dwarf_error(dbg, err, DW_DLE_ALLOC_FAIL);
                return (DW_DLV_ERROR);
            }
            memset(currfile,0,sizeof(struct Dwarf_File_Entry_s));
            /*  Insert early so in case of error we can find
                and free the record. */
            _dwarf_add_to_files_list(line_context,currfile);

            currfile->fi_file_name = line_ptr;
            resl = _dwarf_check_string_valid(dbg,
                data_start,line_ptr,line_ptr_end,err);
            if (resl != DW_DLV_OK) {
                return resl;
            }
            line_ptr = line_ptr + strlen((char *) line_ptr) + 1;
            DECODE_LEB128_UWORD_CK(line_ptr, utmp,dbg,err,line_ptr_end);
            dir_index = (Dwarf_Word) utmp;
            if (dir_index > line_context->lc_include_directories_count) {
                _dwarf_error(dbg, err, DW_DLE_DIR_INDEX_BAD);
                return (DW_DLV_ERROR);
            }
            currfile->fi_dir_index = dir_index;

            DECODE_LEB128_UWORD_CK(line_ptr,lastmod,
                dbg,err, line_ptr_end);
            currfile->fi_time_last_mod = lastmod;

            /* Skip over file length. */
            DECODE_LEB128_UWORD_CK(line_ptr,file_length,
                dbg,err, line_ptr_end);
            currfile->fi_file_length = file_length;
            if (line_ptr >= line_ptr_end) {
                _dwarf_error(dbg, err, DW_DLE_LINE_NUMBER_HEADER_ERROR);
                return (DW_DLV_ERROR);
            }
        }
        /* Skip trailing nul byte */
        ++line_ptr;
    } else if (version == EXPERIMENTAL_LINE_TABLES_VERSION) {
        if (*line_ptr != 0) {
            _dwarf_error(dbg, err, DW_DLE_LINE_NUMBER_HEADER_ERROR);
            return (DW_DLV_ERROR);
        }
        line_ptr++;
    } else {
        /* No old style filenames entries. */
    }
    if(line_ptr > line_ptr_end) {
        _dwarf_error(dbg, err, DW_DLE_LINE_OFFSET_BAD);
        return DW_DLV_ERROR;
    }

    if (version == EXPERIMENTAL_LINE_TABLES_VERSION) {
        static unsigned char expbytes[5] = {0,0xff,0xff,0x7f, 0x7f };
        Dwarf_Unsigned logicals_table_offset = 0;
        Dwarf_Unsigned actuals_table_offset = 0;
        unsigned i = 0;

        for ( ; i < 5; ++i) {
            if (*line_ptr != expbytes[i]) {
                _dwarf_error(dbg, err, DW_DLE_LINE_NUMBER_HEADER_ERROR);
                return (DW_DLV_ERROR);
            }
            line_ptr++;
        }
        READ_UNALIGNED_CK(dbg, logicals_table_offset, Dwarf_Unsigned,
            line_ptr, local_length_size,err,line_ptr_end);
        line_context->lc_logicals_table_offset = logicals_table_offset;
        line_ptr += local_length_size;
        READ_UNALIGNED_CK(dbg, actuals_table_offset, Dwarf_Unsigned,
            line_ptr, local_length_size,err,line_ptr_end);
        line_context->lc_actuals_table_offset = actuals_table_offset;
        line_ptr += local_length_size;
        if(line_ptr > line_ptr_end) {
            _dwarf_error(dbg, err, DW_DLE_LINE_OFFSET_BAD);
            return DW_DLV_ERROR;
        }
    }

    if (version == DW_LINE_VERSION5 ||
        version == EXPERIMENTAL_LINE_TABLES_VERSION) {
        /* DWARF 5. */
        Dwarf_Unsigned directory_format_count = 0;
        Dwarf_Unsigned *directory_entry_types = 0;
        Dwarf_Unsigned *directory_entry_forms = 0;
        Dwarf_Unsigned directories_count = 0;
        Dwarf_Unsigned i = 0;
        Dwarf_Unsigned j = 0;
        directory_format_count = *(unsigned char *) line_ptr;
        line_ptr = line_ptr + sizeof(Dwarf_Small);
        directory_entry_types = malloc(sizeof(Dwarf_Unsigned) *
            directory_format_count);
        if (directory_entry_types == NULL) {
            _dwarf_error(dbg, err, DW_DLE_ALLOC_FAIL);
            return (DW_DLV_ERROR);
        }
        directory_entry_forms = malloc(sizeof(Dwarf_Unsigned) *
            directory_format_count);
        if (directory_entry_forms == NULL) {
            free(directory_entry_types);
            _dwarf_error(dbg, err, DW_DLE_ALLOC_FAIL);
            return (DW_DLV_ERROR);
        }
        for (i = 0; i < directory_format_count; i++) {
            DECODE_LEB128_UWORD_CK(line_ptr, directory_entry_types[i],
                dbg,err,line_ptr_end);
            DECODE_LEB128_UWORD_CK(line_ptr, directory_entry_forms[i],
                dbg,err,line_ptr_end);
        }
        DECODE_LEB128_UWORD_CK(line_ptr, directories_count,
            dbg,err,line_ptr_end);
        line_context->lc_include_directories =
            malloc(sizeof(Dwarf_Small *) * directories_count);
        if (line_context->lc_include_directories == NULL) {
            free(directory_entry_types);
            free(directory_entry_forms);
            _dwarf_error(dbg, err, DW_DLE_ALLOC_FAIL);
            return (DW_DLV_ERROR);
        }
        if (directory_format_count ==0 &&
            directories_count > 0) {
            _dwarf_error(dbg, err, DW_DLE_DIRECTORY_FORMAT_COUNT_VS_DIRECTORIES_MISMATCH);
            return (DW_DLV_ERROR);
        }
        memset(line_context->lc_include_directories, 0,
            sizeof(Dwarf_Small *) * directories_count);

        for(i = 0; i < directories_count; i++) {
            for (j = 0; j < directory_format_count; j++) {

                switch (directory_entry_types[j]) {
                case DW_LNCT_path: {
                    char *inc_dir_ptr = 0;
                    res = _dwarf_decode_line_string_form(dbg,
                        directory_entry_forms[j],
                        local_length_size,
                        &line_ptr,
                        line_ptr_end,
                        &inc_dir_ptr,
                        err);
                    if (res != DW_DLV_OK) {
                        free(directory_entry_types);
                        free(directory_entry_forms);
                        return res;
                    }
                    line_context->lc_include_directories[i] = inc_dir_ptr;
                    break;
                }
                default:
                    free(directory_entry_types);
                    free(directory_entry_forms);
                    _dwarf_error(dbg, err, DW_DLE_LINE_NUMBER_HEADER_ERROR);
                    return (DW_DLV_ERROR);
                }
            }
            if (line_ptr > line_ptr_end) {
                free(directory_entry_types);
                free(directory_entry_forms);
                _dwarf_error(dbg, err,
                    DW_DLE_LINE_NUMBER_HEADER_ERROR);
                return (DW_DLV_ERROR);
            }
        }
        free(directory_entry_types);
        free(directory_entry_forms);
        line_context->lc_include_directories_count = directories_count;
    }

    if (version == DW_LINE_VERSION5 ||
        version == EXPERIMENTAL_LINE_TABLES_VERSION) {
        /* DWARF 5. */
        Dwarf_Unsigned filename_format_count = 0;
        Dwarf_Unsigned *filename_entry_types = 0;
        Dwarf_Unsigned *filename_entry_forms = 0;
        Dwarf_Unsigned files_count = 0;
        Dwarf_Unsigned i = 0;
        Dwarf_Unsigned j = 0;

        filename_format_count = *(unsigned char *) line_ptr;
        line_ptr = line_ptr + sizeof(Dwarf_Small);
        filename_entry_types = malloc(sizeof(Dwarf_Unsigned) *
            filename_format_count);
        if (filename_entry_types == NULL) {
            _dwarf_error(dbg, err, DW_DLE_ALLOC_FAIL);
            return (DW_DLV_ERROR);
        }
        filename_entry_forms = malloc(sizeof(Dwarf_Unsigned) *
            filename_format_count);
        if (filename_entry_forms == NULL) {
            free(filename_entry_types);
            _dwarf_error(dbg, err, DW_DLE_ALLOC_FAIL);
            return (DW_DLV_ERROR);
        }
        for (i = 0; i < filename_format_count; i++) {
            DECODE_LEB128_UWORD_CK(line_ptr, filename_entry_types[i],
                dbg,err,line_ptr_end);
            DECODE_LEB128_UWORD_CK(line_ptr, filename_entry_forms[i],
                dbg,err,line_ptr_end);
        }
        DECODE_LEB128_UWORD_CK(line_ptr, files_count,
            dbg,err,line_ptr_end);

        for (i = 0; i < files_count; i++) {
            Dwarf_File_Entry curline = 0;
            curline = (Dwarf_File_Entry)
                malloc(sizeof(struct Dwarf_File_Entry_s));
            if (curline == NULL) {
                free(filename_entry_types);
                _dwarf_error(dbg, err, DW_DLE_ALLOC_FAIL);
                return (DW_DLV_ERROR);
            }
            memset(curline,0,sizeof(*curline));
            _dwarf_add_to_files_list(line_context,curline);
            for(j = 0; j < filename_format_count; j++) {
                Dwarf_Unsigned dirindex = 0;
                switch (filename_entry_types[j]) {
                case DW_LNCT_path:
                    res = _dwarf_decode_line_string_form(dbg,
                        filename_entry_forms[j],
                        local_length_size,
                        &line_ptr,
                        line_ptr_end,
                        (char **)&curline->fi_file_name,
                        err);
                    if (res != DW_DLV_OK) {
                        free(filename_entry_types);
                        free(filename_entry_forms);
                        return res;
                    }
                    break;
                case DW_LNCT_directory_index:
                    res = _dwarf_decode_line_udata_form(dbg,
                        filename_entry_forms[j],
                        &line_ptr,
                        &dirindex,
                        line_ptr_end,
                        err);
                    if (res != DW_DLV_OK) {
                        free(filename_entry_types);
                        free(filename_entry_forms);
                        return res;
                    }
                    curline->fi_dir_index = dirindex;
                    break;
                case DW_LNCT_timestamp:
                    res = _dwarf_decode_line_udata_form(dbg,
                        filename_entry_forms[j],
                        &line_ptr,
                        &curline->fi_time_last_mod,
                        line_ptr_end,
                        err);
                    if (res != DW_DLV_OK) {
                        free(filename_entry_types);
                        free(filename_entry_forms);
                        return res;
                    }
                    break;
                case DW_LNCT_size:
                    res = _dwarf_decode_line_udata_form(dbg,
                        filename_entry_forms[j],
                        &line_ptr,
                        &curline->fi_file_length,
                        line_ptr_end,
                        err);
                    if (res != DW_DLV_OK) {
                        free(filename_entry_types);
                        free(filename_entry_forms);
                        return res;
                    }
                    break;
                case DW_LNCT_MD5: /* Not yet implemented */
                default:
                    free(filename_entry_types);
                    free(filename_entry_forms);
                    _dwarf_error(dbg, err, DW_DLE_LINE_NUMBER_HEADER_ERROR);
                    return (DW_DLV_ERROR);
                }
                if (line_ptr > line_ptr_end) {
                    free(filename_entry_types);
                    free(filename_entry_forms);
                    _dwarf_error(dbg, err, DW_DLE_LINE_NUMBER_HEADER_ERROR);
                    return (DW_DLV_ERROR);
                }
            }
        }
        free(filename_entry_types);
        free(filename_entry_forms);
    }
    /* For two-level line tables, read the subprograms table. */
    if (version == EXPERIMENTAL_LINE_TABLES_VERSION) {
        Dwarf_Unsigned subprog_format_count = 0;
        Dwarf_Unsigned *subprog_entry_types = 0;
        Dwarf_Unsigned *subprog_entry_forms = 0;
        Dwarf_Unsigned subprogs_count = 0;
        Dwarf_Unsigned i = 0;
        Dwarf_Unsigned j = 0;

        if (line_ptr > line_ptr_end) {
            _dwarf_error(dbg, err, DW_DLE_LINE_NUMBER_HEADER_ERROR);
            return (DW_DLV_ERROR);
        }
        subprog_format_count = *(unsigned char *) line_ptr;
        line_ptr = line_ptr + sizeof(Dwarf_Small);
        subprog_entry_types = malloc(sizeof(Dwarf_Unsigned) *
            subprog_format_count);
        if (subprog_entry_types == NULL) {
            _dwarf_error(dbg, err, DW_DLE_ALLOC_FAIL);
            return (DW_DLV_ERROR);
        }
        subprog_entry_forms = malloc(sizeof(Dwarf_Unsigned) *
            subprog_format_count);
        if (subprog_entry_forms == NULL) {
            free(subprog_entry_types);
            _dwarf_error(dbg, err, DW_DLE_ALLOC_FAIL);
            return (DW_DLV_ERROR);
        }

        for (i = 0; i < subprog_format_count; i++) {
            DECODE_LEB128_UWORD_CK(line_ptr, subprog_entry_types[i],
                dbg,err,line_ptr_end);
            DECODE_LEB128_UWORD_CK(line_ptr, subprog_entry_forms[i],
                dbg,err,line_ptr_end);
        }
        DECODE_LEB128_UWORD_CK(line_ptr, subprogs_count,
            dbg,err,line_ptr_end);
        line_context->lc_subprogs =
            malloc(sizeof(struct Dwarf_Subprog_Entry_s) * subprogs_count);
        if (line_context->lc_subprogs == NULL) {
            free(subprog_entry_types);
            free(subprog_entry_forms);
            _dwarf_error(dbg, err, DW_DLE_ALLOC_FAIL);
            return (DW_DLV_ERROR);
        }
        memset(line_context->lc_subprogs, 0,
            sizeof(struct Dwarf_Subprog_Entry_s) * subprogs_count);
        for (i = 0; i < subprogs_count; i++) {
            struct Dwarf_Subprog_Entry_s *curline =
                line_context->lc_subprogs + i;
            for (j = 0; j < subprog_format_count; j++) {
                switch (subprog_entry_types[j]) {
                case DW_LNCT_subprogram_name:
                    res = _dwarf_decode_line_string_form(dbg,
                        subprog_entry_forms[j],
                        local_length_size,
                        &line_ptr,
                        line_ptr_end,
                        (char **)&curline->ds_subprog_name,
                        err);
                    if (res != DW_DLV_OK) {
                        free(subprog_entry_types);
                        free(subprog_entry_forms);
                        return res;
                    }
                    break;
                case DW_LNCT_decl_file:
                    res = _dwarf_decode_line_udata_form(dbg,
                        subprog_entry_forms[j],
                        &line_ptr,
                        &curline->ds_decl_file,
                        line_ptr_end,
                        err);
                    if (res != DW_DLV_OK) {
                        free(subprog_entry_forms);
                        free(subprog_entry_types);
                        return res;
                    }
                    break;
                case DW_LNCT_decl_line:
                    res = _dwarf_decode_line_udata_form(dbg,
                        subprog_entry_forms[j],
                        &line_ptr,
                        &curline->ds_decl_line,
                        line_ptr_end,
                        err);
                    if (res != DW_DLV_OK) {
                        free(subprog_entry_forms);
                        free(subprog_entry_types);
                        return res;
                    }
                    break;
                default:
                    free(subprog_entry_forms);
                    free(subprog_entry_types);
                    _dwarf_error(dbg, err, DW_DLE_LINE_NUMBER_HEADER_ERROR);
                    return (DW_DLV_ERROR);
                }
                if (line_ptr >= line_ptr_end) {
                    free(subprog_entry_types);
                    free(subprog_entry_forms);
                    _dwarf_error(dbg, err, DW_DLE_LINE_NUMBER_HEADER_ERROR);
                    return (DW_DLV_ERROR);
                }
            }
        }

        free(subprog_entry_types);
        free(subprog_entry_forms);
        line_context->lc_subprogs_count = subprogs_count;
    }
    if (version == EXPERIMENTAL_LINE_TABLES_VERSION) {
        lp_begin = line_context->lc_line_prologue_start +
            line_context->lc_logicals_table_offset;
    } else {
        lp_begin = line_context->lc_line_prologue_start +
            line_context->lc_prologue_length;
    }
    if(line_ptr > line_ptr_end) {
        _dwarf_error(dbg, err, DW_DLE_LINE_OFFSET_BAD);
        return DW_DLV_ERROR;
    }
    if (line_ptr != lp_begin) {
        if (line_ptr > lp_begin) {
            _dwarf_error(dbg, err, DW_DLE_LINE_PROLOG_LENGTH_BAD);
            return (DW_DLV_ERROR);
        } else {
            /*  Bug in compiler. These
                bytes are really part of the instruction
                stream.  The line_context->lc_prologue_length is
                wrong (12 too high).  */
            if (bogus_bytes_ptr) {
                *bogus_bytes_ptr = line_ptr;
            }
            if (bogus_bytes) {
                /*  How far off things are. We expect the
                    value 12 ! */
                *bogus_bytes = (lp_begin - line_ptr);
            }
        }
        /*  Ignore the lp_begin calc. Assume line_ptr right.
            Making up for compiler bug. */
        lp_begin = line_ptr;
    }
    line_context->lc_line_ptr_start = lp_begin;
    if (line_context->lc_actuals_table_offset) {
        /* This means two tables. */
        line_context->lc_table_count = 2;
    } else {
        if (line_context->lc_line_ptr_end > lp_begin) {
            line_context->lc_table_count = 1;
        } else {
            line_context->lc_table_count = 0;
        }
    }
    *updated_data_start_out = lp_begin;
    return DW_DLV_OK;
}


/*  Read one line table program. For two-level line tables, this
    function is called once for each table. */
static int
read_line_table_program(Dwarf_Debug dbg,
    Dwarf_Small *line_ptr,
    Dwarf_Small *line_ptr_end,
    UNUSEDARG Dwarf_Small *orig_line_ptr,
    UNUSEDARG Dwarf_Small *section_start,
    Dwarf_Line_Context line_context,
    Dwarf_Half address_size,
    Dwarf_Bool doaddrs, /* Only true if SGI IRIX rqs calling. */
    Dwarf_Bool dolines,
    Dwarf_Bool is_single_table,
    Dwarf_Bool is_actuals_table,
    Dwarf_Error *error,
    UNUSEDARG int *err_count_out)
{
    Dwarf_Word i = 0;
    Dwarf_File_Entry cur_file_entry = 0;
    Dwarf_Line *logicals = line_context->lc_linebuf_logicals;
    Dwarf_Unsigned logicals_count = line_context->lc_linecount_logicals;

    struct Dwarf_Line_Registers_s regs;

    /*  This is a pointer to the current line being added to the line
        matrix. */
    Dwarf_Line curr_line = 0;

    /*  These variables are used to decode leb128 numbers. Leb128_num
        holds the decoded number, and leb128_length is its length in
        bytes. */
    Dwarf_Word leb128_num = 0;
    Dwarf_Sword advance_line = 0;

    /*  This is the operand of the latest fixed_advance_pc extended
        opcode. */
    Dwarf_Half fixed_advance_pc = 0;

    /*  Counts the number of lines in the line matrix. */
    Dwarf_Word line_count = 0;

    /*  This is the length of an extended opcode instr.  */
    Dwarf_Word instr_length = 0;


    /*  Used to chain together pointers to line table entries that are
        later used to create a block of Dwarf_Line entries. */
    Dwarf_Chain chain_line = NULL;
    Dwarf_Chain head_chain = NULL;
    Dwarf_Chain curr_chain = NULL;

    /*  This points to a block of Dwarf_Lines, a pointer to which is
        returned in linebuf. */
    Dwarf_Line *block_line = 0;

    /*  Mark a line record as being DW_LNS_set_address */
    Dwarf_Bool is_addr_set = false;

    /*  Initialize the one state machine variable that depends on the
        prefix.  */
    _dwarf_set_line_table_regs_default_values(&regs,
        line_context->lc_default_is_stmt);

    /* Start of statement program.  */
    while (line_ptr < line_ptr_end) {
        int type = 0;
        Dwarf_Small opcode = 0;

#ifdef PRINTING_DETAILS
        dwarf_printf(dbg,
            " [0x%06" DW_PR_DSx "] ",
            (Dwarf_Signed) (line_ptr - section_start));
#endif /* PRINTING_DETAILS */
        opcode = *(Dwarf_Small *) line_ptr;
        line_ptr++;
        /* 'type' is the output */
        WHAT_IS_OPCODE(type, opcode, line_context->lc_opcode_base,
            line_context->lc_opcode_length_table, line_ptr,
            line_context->lc_std_op_count);

        if (type == LOP_DISCARD) {
            int oc = 0;
            int opcnt = line_context->lc_opcode_length_table[opcode];

#ifdef PRINTING_DETAILS
            dwarf_printf(dbg,
                "*** DWARF CHECK: DISCARD standard opcode %d "
                "with %d operands: "
                "not understood.", opcode, opcnt);
            *err_count_out += 1;
#endif /* PRINTING_DETAILS */
            for (oc = 0; oc < opcnt; oc++) {
                /*  Read and discard operands we don't
                    understand.
                    arbitrary choice of unsigned read.
                    signed read would work as well.    */
                UNUSEDARG Dwarf_Unsigned utmp2 = 0;

                DECODE_LEB128_UWORD_CK(line_ptr, utmp2,
                    dbg,error,line_ptr_end);
#ifdef PRINTING_DETAILS
                dwarf_printf(dbg,
                    " %" DW_PR_DUu
                    " (0x%" DW_PR_XZEROS DW_PR_DUx ")",
                    (Dwarf_Unsigned) utmp2,
                    (Dwarf_Unsigned) utmp2);
#endif /* PRINTING_DETAILS */
            }
#ifdef PRINTING_DETAILS
            dwarf_printf(dbg,"***\n");
#endif /* PRINTING_DETAILS */
        } else if (type == LOP_SPECIAL) {
            /*  This op code is a special op in the object, no matter
                that it might fall into the standard op range in this
                compile. That is, these are special opcodes between
                opcode_base and MAX_LINE_OP_CODE.  (including
                opcode_base and MAX_LINE_OP_CODE) */
#ifdef PRINTING_DETAILS
            char special[50];
            unsigned origop = opcode;
#endif /* PRINTING_DETAILS */
            Dwarf_Unsigned operation_advance = 0;

            opcode = opcode - line_context->lc_opcode_base;
            operation_advance = (opcode / line_context->lc_line_range);

            if (line_context->lc_maximum_ops_per_instruction < 2) {
                regs.lr_address = regs.lr_address + (operation_advance *
                    line_context->lc_minimum_instruction_length);
            } else {
                regs.lr_address = regs.lr_address +
                    (line_context->lc_minimum_instruction_length *
                    ((regs.lr_op_index + operation_advance)/
                    line_context->lc_maximum_ops_per_instruction));
                regs.lr_op_index = (regs.lr_op_index +operation_advance)%
                    line_context->lc_maximum_ops_per_instruction;
            }

            regs.lr_line = regs.lr_line + line_context->lc_line_base +
                opcode % line_context->lc_line_range;
#ifdef PRINTING_DETAILS
            sprintf(special, "Specialop %3u", origop);
            print_line_detail(dbg,special,
                opcode,line_count+1, &regs,is_single_table, is_actuals_table);
#endif /* PRINTING_DETAILS */

            if (dolines) {
                curr_line =
                    (Dwarf_Line) _dwarf_get_alloc(dbg, DW_DLA_LINE, 1);
                if (curr_line == NULL) {
                    _dwarf_free_chain_entries(dbg,head_chain,line_count);
                    _dwarf_error(dbg, error, DW_DLE_ALLOC_FAIL);
                    return (DW_DLV_ERROR);
                }

                /* Mark a line record as being DW_LNS_set_address */
                curr_line->li_addr_line.li_l_data.li_is_addr_set = is_addr_set;
                is_addr_set = false;

                curr_line->li_address = regs.lr_address;
                curr_line->li_addr_line.li_l_data.li_file =
                    (Dwarf_Sword) regs.lr_file;
                curr_line->li_addr_line.li_l_data.li_line =
                    (Dwarf_Sword) regs.lr_line;
                curr_line->li_addr_line.li_l_data.li_column =
                    (Dwarf_Half) regs.lr_column;
                curr_line->li_addr_line.li_l_data.li_is_stmt =
                    regs.lr_is_stmt;
                curr_line->li_addr_line.li_l_data.li_basic_block =
                    regs.lr_basic_block;
                curr_line->li_addr_line.li_l_data.li_end_sequence =
                    curr_line->li_addr_line.li_l_data.
                    li_epilogue_begin = regs.lr_epilogue_begin;
                curr_line->li_addr_line.li_l_data.li_prologue_end =
                    regs.lr_prologue_end;
                curr_line->li_addr_line.li_l_data.li_isa = regs.lr_isa;
                curr_line->li_addr_line.li_l_data.li_discriminator =
                    regs.lr_discriminator;
                curr_line->li_addr_line.li_l_data.li_call_context =
                    regs.lr_call_context;
                curr_line->li_addr_line.li_l_data.li_subprogram =
                    regs.lr_subprogram;
                curr_line->li_context = line_context;
                curr_line->li_is_actuals_table = is_actuals_table;
                line_count++;

                chain_line = (Dwarf_Chain)
                    _dwarf_get_alloc(dbg, DW_DLA_CHAIN, 1);
                if (chain_line == NULL) {
                    _dwarf_free_chain_entries(dbg,head_chain,line_count);
                    _dwarf_error(dbg, error, DW_DLE_ALLOC_FAIL);
                    return (DW_DLV_ERROR);
                }
                chain_line->ch_item = curr_line;
                _dwarf_update_chain_list(chain_line,&head_chain,&curr_chain);
            }

            regs.lr_basic_block = false;
            regs.lr_prologue_end = false;
            regs.lr_epilogue_begin = false;
            regs.lr_discriminator = 0;
        } else if (type == LOP_STANDARD) {
            switch (opcode) {

            case DW_LNS_copy:{

#ifdef PRINTING_DETAILS
                print_line_detail(dbg,"DW_LNS_copy",
                    opcode,line_count+1, &regs,is_single_table, is_actuals_table);
#endif /* PRINTING_DETAILS */
                if (dolines) {
                    curr_line = (Dwarf_Line) _dwarf_get_alloc(dbg,
                        DW_DLA_LINE, 1);
                    if (curr_line == NULL) {
                        _dwarf_free_chain_entries(dbg,head_chain,line_count);
                        _dwarf_error(dbg, error, DW_DLE_ALLOC_FAIL);
                        return (DW_DLV_ERROR);
                    }

                    /* Mark a line record as being DW_LNS_set_address */
                    curr_line->li_addr_line.li_l_data.li_is_addr_set =
                        is_addr_set;
                    is_addr_set = false;

                    curr_line->li_address = regs.lr_address;
                    curr_line->li_addr_line.li_l_data.li_file =
                        (Dwarf_Sword) regs.lr_file;
                    curr_line->li_addr_line.li_l_data.li_line =
                        (Dwarf_Sword) regs.lr_line;
                    curr_line->li_addr_line.li_l_data.li_column =
                        (Dwarf_Half) regs.lr_column;
                    curr_line->li_addr_line.li_l_data.li_is_stmt =
                        regs.lr_is_stmt;
                    curr_line->li_addr_line.li_l_data.
                        li_basic_block = regs.lr_basic_block;
                    curr_line->li_addr_line.li_l_data.
                        li_end_sequence = regs.lr_end_sequence;
                    curr_line->li_context = line_context;
                    curr_line->li_is_actuals_table = is_actuals_table;
                    curr_line->li_addr_line.li_l_data.
                        li_epilogue_begin = regs.lr_epilogue_begin;
                    curr_line->li_addr_line.li_l_data.
                        li_prologue_end = regs.lr_prologue_end;
                    curr_line->li_addr_line.li_l_data.li_isa = regs.lr_isa;
                    curr_line->li_addr_line.li_l_data.li_discriminator =
                        regs.lr_discriminator;
                    curr_line->li_addr_line.li_l_data.li_call_context =
                        regs.lr_call_context;
                    curr_line->li_addr_line.li_l_data.li_subprogram =
                        regs.lr_subprogram;
                    line_count++;

                    chain_line = (Dwarf_Chain)
                        _dwarf_get_alloc(dbg, DW_DLA_CHAIN, 1);
                    if (chain_line == NULL) {
                        _dwarf_free_chain_entries(dbg,head_chain,line_count);
                        _dwarf_error(dbg, error, DW_DLE_ALLOC_FAIL);
                        return (DW_DLV_ERROR);
                    }
                    chain_line->ch_item = curr_line;
                    _dwarf_update_chain_list(chain_line,&head_chain,&curr_chain);
                }

                regs.lr_basic_block = false;
                regs.lr_prologue_end = false;
                regs.lr_epilogue_begin = false;
                regs.lr_discriminator = 0;
                }
                break;
            case DW_LNS_advance_pc:{
                Dwarf_Unsigned utmp2 = 0;

                DECODE_LEB128_UWORD_CK(line_ptr, utmp2,
                    dbg,error,line_ptr_end);

#ifdef PRINTING_DETAILS
                dwarf_printf(dbg,
                    "DW_LNS_advance_pc val %"
                    DW_PR_DSd " 0x%"
                    DW_PR_XZEROS DW_PR_DUx "\n",
                    (Dwarf_Signed) (Dwarf_Word) utmp2,
                    (Dwarf_Unsigned) (Dwarf_Word) utmp2);
#endif /* PRINTING_DETAILS */
                leb128_num = (Dwarf_Word) utmp2;
                regs.lr_address = regs.lr_address +
                    line_context->lc_minimum_instruction_length *
                    leb128_num;
                }
                break;
            case DW_LNS_advance_line:{
                Dwarf_Signed stmp = 0;

                DECODE_LEB128_SWORD_CK(line_ptr, stmp,
                    dbg,error,line_ptr_end);
                advance_line = (Dwarf_Sword) stmp;

#ifdef PRINTING_DETAILS
                dwarf_printf(dbg,
                    "DW_LNS_advance_line val %" DW_PR_DSd " 0x%"
                    DW_PR_XZEROS DW_PR_DSx "\n",
                    (Dwarf_Signed) advance_line,
                    (Dwarf_Signed) advance_line);
#endif /* PRINTING_DETAILS */
                regs.lr_line = regs.lr_line + advance_line;
                }
                break;
            case DW_LNS_set_file:{
                Dwarf_Unsigned utmp2 = 0;

                DECODE_LEB128_UWORD_CK(line_ptr, utmp2,
                    dbg,error,line_ptr_end);
                regs.lr_file = (Dwarf_Word) utmp2;
#ifdef PRINTING_DETAILS
                dwarf_printf(dbg,
                    "DW_LNS_set_file  %ld\n", (long) regs.lr_file);
#endif /* PRINTING_DETAILS */
                }
                break;
            case DW_LNS_set_column:{
                Dwarf_Unsigned utmp2 = 0;

                DECODE_LEB128_UWORD_CK(line_ptr, utmp2,
                    dbg,error,line_ptr_end);
                regs.lr_column = (Dwarf_Word) utmp2;
#ifdef PRINTING_DETAILS
                dwarf_printf(dbg,
                    "DW_LNS_set_column val %" DW_PR_DSd " 0x%"
                    DW_PR_XZEROS DW_PR_DSx "\n",
                    (Dwarf_Signed) regs.lr_column,
                    (Dwarf_Signed) regs.lr_column);
#endif /* PRINTING_DETAILS */
                }
                break;
            case DW_LNS_negate_stmt:{
                regs.lr_is_stmt = !regs.lr_is_stmt;
#ifdef PRINTING_DETAILS
                dwarf_printf(dbg,
                    "DW_LNS_negate_stmt\n");
#endif /* PRINTING_DETAILS */
                }
                break;
            case DW_LNS_set_basic_block:{
                regs.lr_basic_block = true;
#ifdef PRINTING_DETAILS
                dwarf_printf(dbg,
                    "DW_LNS_set_basic_block\n");
#endif /* PRINTING_DETAILS */
                }
                break;

            case DW_LNS_const_add_pc:{
                opcode = MAX_LINE_OP_CODE - line_context->lc_opcode_base;
                if (line_context->lc_maximum_ops_per_instruction < 2) {
                    Dwarf_Unsigned operation_advance =
                        (opcode / line_context->lc_line_range);
                    regs.lr_address = regs.lr_address +
                        line_context->lc_minimum_instruction_length *
                            operation_advance;
                } else {
                    Dwarf_Unsigned operation_advance =
                        (opcode / line_context->lc_line_range);
                    regs.lr_address = regs.lr_address +
                        line_context->lc_minimum_instruction_length *
                        ((regs.lr_op_index + operation_advance)/
                        line_context->lc_maximum_ops_per_instruction);
                    regs.lr_op_index = (regs.lr_op_index +operation_advance)%
                        line_context->lc_maximum_ops_per_instruction;
                }
#ifdef PRINTING_DETAILS
                dwarf_printf(dbg,
                    "DW_LNS_const_add_pc new address 0x%"
                    DW_PR_XZEROS DW_PR_DSx "\n",
                    (Dwarf_Signed) regs.lr_address);
#endif /* PRINTING_DETAILS */
                }
                break;
            case DW_LNS_fixed_advance_pc:{
                READ_UNALIGNED_CK(dbg, fixed_advance_pc, Dwarf_Half,
                    line_ptr, sizeof(Dwarf_Half),error,line_ptr_end);
                line_ptr += sizeof(Dwarf_Half);
                regs.lr_address = regs.lr_address + fixed_advance_pc;
                regs.lr_op_index = 0;
#ifdef PRINTING_DETAILS
                dwarf_printf(dbg,
                    "DW_LNS_fixed_advance_pc val %" DW_PR_DSd
                    " 0x%" DW_PR_XZEROS DW_PR_DSx
                    " new address 0x%" DW_PR_XZEROS DW_PR_DSx "\n",
                    (Dwarf_Signed) fixed_advance_pc,
                    (Dwarf_Signed) fixed_advance_pc,
                    (Dwarf_Signed) regs.lr_address);
#endif /* PRINTING_DETAILS */
                }
                break;

                /* New in DWARF3 */
            case DW_LNS_set_prologue_end:{
                regs.lr_prologue_end = true;
                }
                break;
                /* New in DWARF3 */
            case DW_LNS_set_epilogue_begin:{
                regs.lr_epilogue_begin = true;
#ifdef PRINTING_DETAILS
                dwarf_printf(dbg,
                    "DW_LNS_set_prologue_end set true.\n");
#endif /* PRINTING_DETAILS */
                }
                break;

                /* New in DWARF3 */
            case DW_LNS_set_isa:{
                Dwarf_Unsigned utmp2 = 0;

                DECODE_LEB128_UWORD_CK(line_ptr, utmp2,
                    dbg,error,line_ptr_end);
                regs.lr_isa = utmp2;

#ifdef PRINTING_DETAILS
                dwarf_printf(dbg,
                    "DW_LNS_set_isa new value 0x%"
                    DW_PR_XZEROS DW_PR_DUx ".\n",
                    (Dwarf_Unsigned) utmp2);
#endif /* PRINTING_DETAILS */
                if (regs.lr_isa != utmp2) {
                    /*  The value of the isa did not fit in our
                        local so we record it wrong. declare an
                        error. */
                    _dwarf_free_chain_entries(dbg,head_chain,line_count);
                    _dwarf_error(dbg, error,
                        DW_DLE_LINE_NUM_OPERANDS_BAD);
                    return (DW_DLV_ERROR);
                }
                }
                break;

                /*  Experimental two-level line tables */
                /*  DW_LNS_set_address_from_logical and
                    DW_LNS_set_subprogram
                    share the same opcode. Disambiguate by checking
                    is_actuals_table. */
            case DW_LNS_set_subprogram:
                if (is_actuals_table) {
                    /* DW_LNS_set_address_from_logical */
                    Dwarf_Signed stmp = 0;

                    DECODE_LEB128_SWORD_CK(line_ptr, stmp,
                        dbg,error,line_ptr_end);
                    advance_line = (Dwarf_Sword) stmp;
                    regs.lr_line = regs.lr_line + advance_line;
                    if (regs.lr_line >= 1 &&
                        regs.lr_line - 1 < logicals_count) {
                        regs.lr_address =
                            logicals[regs.lr_line - 1]->li_address;
                        regs.lr_op_index = 0;
#ifdef PRINTING_DETAILS
                        dwarf_printf(dbg,"DW_LNS_set_address_from_logical "
                            "%" DW_PR_DSd " 0x%" DW_PR_XZEROS DW_PR_DSx,
                            stmp,stmp);
                        dwarf_printf(dbg,"  newaddr="
                            " 0x%" DW_PR_XZEROS DW_PR_DUx ".\n",
                            regs.lr_address);
#endif /* PRINTING_DETAILS */
                    } else {
#ifdef PRINTING_DETAILS
                        dwarf_printf(dbg,"DW_LNS_set_address_from_logical line is "
                            "%" DW_PR_DSd " 0x%" DW_PR_XZEROS DW_PR_DSx ".\n",
                            (Dwarf_Signed)regs.lr_line,
                            (Dwarf_Signed)regs.lr_line);
#endif /* PRINTING_DETAILS */
                    }
                } else {
                    /* DW_LNS_set_subprogram, building logicals table.  */
                    Dwarf_Unsigned utmp2 = 0;

                    regs.lr_call_context = 0;
                    DECODE_LEB128_UWORD_CK(line_ptr, utmp2,
                        dbg,error,line_ptr_end);
                    regs.lr_subprogram = (Dwarf_Word) utmp2;
#ifdef PRINTING_DETAILS
                    dwarf_printf(dbg,"DW_LNS_set_subprogram "
                        "%" DW_PR_DSd " 0x%" DW_PR_XZEROS DW_PR_DSx "\n",
                        (Dwarf_Signed)utmp2,(Dwarf_Signed)utmp2);
#endif /* PRINTING_DETAILS */
                }
                break;

                /* Experimental two-level line tables */
            case DW_LNS_inlined_call: {
                Dwarf_Signed stmp = 0;

                DECODE_LEB128_SWORD_CK(line_ptr, stmp,
                    dbg,error,line_ptr_end);
                regs.lr_call_context = line_count + stmp;
                DECODE_LEB128_UWORD_CK(line_ptr, regs.lr_subprogram,
                    dbg,error,line_ptr_end);

#ifdef PRINTING_DETAILS
                dwarf_printf(dbg,"DW_LNS_inlined_call "
                    "%" DW_PR_DSd " (0x%" DW_PR_XZEROS DW_PR_DSx "),"
                    "%" DW_PR_DSd " (0x%" DW_PR_XZEROS DW_PR_DSx ")",
                    stmp,stmp,
                    (Dwarf_Signed)regs.lr_subprogram,
                    (Dwarf_Signed)regs.lr_subprogram);
                dwarf_printf(dbg,"  callcontext="
                    "%" DW_PR_DSd " (0x%" DW_PR_XZEROS DW_PR_DSx ")\n",
                    (Dwarf_Signed)regs.lr_call_context,
                    (Dwarf_Signed)regs.lr_call_context);
#endif /* PRINTING_DETAILS */
                }
                break;

                /* Experimental two-level line tables */
            case DW_LNS_pop_context: {
                Dwarf_Unsigned logical_num = regs.lr_call_context;
                Dwarf_Chain logical_chain = head_chain;
                Dwarf_Line logical_line = 0;

                if (logical_num > 0 && logical_num <= line_count) {
                    for (i = 1; i < logical_num; i++) {
                        logical_chain = logical_chain->ch_next;
                    }
                    logical_line = (Dwarf_Line) logical_chain->ch_item;
                    regs.lr_file =
                        logical_line->li_addr_line.li_l_data.li_file;
                    regs.lr_line =
                        logical_line->li_addr_line.li_l_data.li_line;
                    regs.lr_column =
                        logical_line->li_addr_line.li_l_data.li_column;
                    regs.lr_discriminator =
                        logical_line->li_addr_line.li_l_data.li_discriminator;
                    regs.lr_is_stmt =
                        logical_line->li_addr_line.li_l_data.li_is_stmt;
                    regs.lr_call_context =
                        logical_line->li_addr_line.li_l_data.li_call_context;
                    regs.lr_subprogram =
                        logical_line->li_addr_line.li_l_data.li_subprogram;
#ifdef PRINTING_DETAILS
                    dwarf_printf(dbg,"DW_LNS_pop_context set from logical "
                        "%" DW_PR_DUu " (0x%" DW_PR_XZEROS DW_PR_DUx ")\n",
                        logical_num,logical_num);
                } else {
                    dwarf_printf(dbg,"DW_LNS_pop_context does nothing, logical"
                        "%" DW_PR_DUu " (0x%" DW_PR_XZEROS DW_PR_DUx ")\n",
                        logical_num,logical_num);
#endif /* PRINTING_DETAILS */
                }
                }
                break;
            } /* End switch (opcode) */

        } else if (type == LOP_EXTENDED) {
            Dwarf_Unsigned utmp3 = 0;
            Dwarf_Small ext_opcode = 0;

            DECODE_LEB128_UWORD_CK(line_ptr, utmp3,
                dbg,error,line_ptr_end);
            instr_length = (Dwarf_Word) utmp3;
            /*  Dwarf_Small is a ubyte and the extended opcode is a
                ubyte, though not stated as clearly in the 2.0.0 spec as
                one might hope. */
            ext_opcode = *(Dwarf_Small *) line_ptr;
            line_ptr++;
            switch (ext_opcode) {

            case DW_LNE_end_sequence:{
                regs.lr_end_sequence = true;
                if (dolines) {
                    curr_line = (Dwarf_Line)
                        _dwarf_get_alloc(dbg, DW_DLA_LINE, 1);
                    if (curr_line == NULL) {
                        _dwarf_free_chain_entries(dbg,head_chain,line_count);
                        _dwarf_error(dbg, error, DW_DLE_ALLOC_FAIL);
                        return (DW_DLV_ERROR);
                    }

#ifdef PRINTING_DETAILS
                    print_line_detail(dbg,"DW_LNE_end_sequence extended",
                        ext_opcode, line_count+1,&regs,
                        is_single_table, is_actuals_table);
#endif /* PRINTING_DETAILS */
                    curr_line->li_address = regs.lr_address;
                    curr_line->li_addr_line.li_l_data.li_file =
                        (Dwarf_Sword) regs.lr_file;
                    curr_line->li_addr_line.li_l_data.li_line =
                        (Dwarf_Sword) regs.lr_line;
                    curr_line->li_addr_line.li_l_data.li_column =
                        (Dwarf_Half) regs.lr_column;
                    curr_line->li_addr_line.li_l_data.li_is_stmt =
                        regs.lr_is_stmt;
                    curr_line->li_addr_line.li_l_data.
                        li_basic_block = regs.lr_basic_block;
                    curr_line->li_addr_line.li_l_data.
                        li_end_sequence = regs.lr_end_sequence;
                    curr_line->li_context = line_context;
                    curr_line->li_is_actuals_table = is_actuals_table;
                    curr_line->li_addr_line.li_l_data.
                        li_epilogue_begin = regs.lr_epilogue_begin;
                    curr_line->li_addr_line.li_l_data.
                        li_prologue_end = regs.lr_prologue_end;
                    curr_line->li_addr_line.li_l_data.li_isa = regs.lr_isa;
                    curr_line->li_addr_line.li_l_data.li_discriminator =
                        regs.lr_discriminator;
                    curr_line->li_addr_line.li_l_data.li_call_context =
                        regs.lr_call_context;
                    curr_line->li_addr_line.li_l_data.li_subprogram =
                        regs.lr_subprogram;
                    line_count++;
                    chain_line = (Dwarf_Chain)
                        _dwarf_get_alloc(dbg, DW_DLA_CHAIN, 1);
                    if (chain_line == NULL) {
                        _dwarf_free_chain_entries(dbg,head_chain,line_count);
                        _dwarf_error(dbg, error, DW_DLE_ALLOC_FAIL);
                        return (DW_DLV_ERROR);
                    }
                    chain_line->ch_item = curr_line;
                    _dwarf_update_chain_list(chain_line,&head_chain,&curr_chain);
                }
                _dwarf_set_line_table_regs_default_values(&regs,
                    line_context->lc_default_is_stmt);
                }
                break;

            case DW_LNE_set_address:{
                READ_UNALIGNED_CK(dbg, regs.lr_address, Dwarf_Addr,
                    line_ptr, address_size,error,line_ptr_end);
                /* Mark a line record as being DW_LNS_set_address */
                is_addr_set = true;
#ifdef PRINTING_DETAILS
                dwarf_printf(dbg,
                    "DW_LNE_set_address address 0x%"
                    DW_PR_XZEROS DW_PR_DUx "\n",
                    (Dwarf_Unsigned) regs.lr_address);
#endif /* PRINTING_DETAILS */
                if (doaddrs) {
                    /* SGI IRIX rqs processing only. */
                    curr_line = (Dwarf_Line) _dwarf_get_alloc(dbg,
                        DW_DLA_LINE, 1);
                    if (curr_line == NULL) {
                        _dwarf_free_chain_entries(dbg,head_chain,line_count);
                        _dwarf_error(dbg, error, DW_DLE_ALLOC_FAIL);
                        return (DW_DLV_ERROR);
                    }

                    /* Mark a line record as being DW_LNS_set_address */
                    curr_line->li_addr_line.li_l_data.li_is_addr_set =
                        is_addr_set;
                    is_addr_set = false;
                    curr_line->li_address = regs.lr_address;
#ifdef __sgi /* SGI IRIX ONLY */
                    curr_line->li_addr_line.li_offset =
                        line_ptr - dbg->de_debug_line.dss_data;
#endif /* __sgi */
                    line_count++;
                    chain_line = (Dwarf_Chain)
                        _dwarf_get_alloc(dbg, DW_DLA_CHAIN, 1);
                    if (chain_line == NULL) {
                        _dwarf_free_chain_entries(dbg,head_chain,line_count);
                        _dwarf_error(dbg, error, DW_DLE_ALLOC_FAIL);
                        return (DW_DLV_ERROR);
                    }
                    chain_line->ch_item = curr_line;

                    _dwarf_update_chain_list(chain_line,&head_chain,&curr_chain);
                }
                regs.lr_op_index = 0;
                line_ptr += address_size;
                }
                break;

            case DW_LNE_define_file:
                if (dolines) {
                    int res = 0;
                    Dwarf_Unsigned value = 0;
                    cur_file_entry = (Dwarf_File_Entry)
                        malloc(sizeof(struct Dwarf_File_Entry_s));
                    if (cur_file_entry == NULL) {
                        _dwarf_error(dbg, error, DW_DLE_ALLOC_FAIL);
                        return (DW_DLV_ERROR);
                    }
                    memset(cur_file_entry,0,sizeof(struct Dwarf_File_Entry_s));
                    _dwarf_add_to_files_list(line_context,cur_file_entry);
                    cur_file_entry->fi_file_name = (Dwarf_Small *) line_ptr;
                    res = _dwarf_check_string_valid(dbg,
                        line_ptr,line_ptr,line_ptr_end,error);
                    if (res != DW_DLV_OK) {
                        _dwarf_free_chain_entries(dbg,head_chain,line_count);
                        return res;
                    }
                    line_ptr = line_ptr + strlen((char *) line_ptr) + 1;

                    DECODE_LEB128_UWORD_CK(line_ptr,value,
                        dbg,error,line_ptr_end);
                    cur_file_entry->fi_dir_index = (Dwarf_Sword)value;
                    DECODE_LEB128_UWORD_CK(line_ptr,value,
                        dbg,error,line_ptr_end);
                    cur_file_entry->fi_time_last_mod = value;
                    DECODE_LEB128_UWORD_CK(line_ptr,value,
                        dbg,error,line_ptr_end);
                    cur_file_entry->fi_file_length = value;
#ifdef PRINTING_DETAILS
                    dwarf_printf(dbg,
                        "DW_LNE_define_file %s \n", cur_file_entry->fi_file_name);
                    dwarf_printf(dbg,
                        "    dir index %d\n", (int) cur_file_entry->fi_dir_index);
                    {
                        time_t tt3 = (time_t) cur_file_entry->fi_time_last_mod;

                        /* ctime supplies newline */
                        dwarf_printf(dbg,
                            "    last time 0x%x %s",
                            (unsigned)tt3, ctime(&tt3));
                    }
                    dwarf_printf(dbg,
                        "    file length %ld 0x%lx\n",
                        (long) cur_file_entry->fi_file_length,
                        (unsigned long) cur_file_entry->fi_file_length);
#endif /* PRINTING_DETAILS */
                }
                break;
            case DW_LNE_set_discriminator:{
                /* New in DWARF4 */
                Dwarf_Unsigned utmp2 = 0;

                DECODE_LEB128_UWORD_CK(line_ptr, utmp2,
                    dbg,error,line_ptr_end);
                regs.lr_discriminator = (Dwarf_Word) utmp2;

#ifdef PRINTING_DETAILS
                dwarf_printf(dbg,
                    "DW_LNE_set_discriminator 0x%"
                    DW_PR_XZEROS DW_PR_DUx "\n",utmp2);
#endif /* PRINTING_DETAILS */
                }
                break;
            default:{
                /*  This is an extended op code we do not know about,
                    other than we know now many bytes it is
                    and the op code and the bytes of operand. */
                Dwarf_Unsigned remaining_bytes = instr_length -1;
                if (instr_length < 1 || remaining_bytes > DW_LNE_LEN_MAX) {
                    _dwarf_free_chain_entries(dbg,head_chain,line_count);
                    _dwarf_error(dbg, error,
                        DW_DLE_LINE_EXT_OPCODE_BAD);
                    return (DW_DLV_ERROR);
                }

#ifdef PRINTING_DETAILS
                dwarf_printf(dbg,
                    "DW_LNE extended op 0x%x ",ext_opcode);
                dwarf_printf(dbg,
                    "Bytecount: %" DW_PR_DUu , (Dwarf_Unsigned)instr_length);
                if (remaining_bytes > 0) {
                    dwarf_printf(dbg,
                        " linedata: 0x");
                    while (remaining_bytes > 0) {
                        dwarf_printf(dbg,
                            "%02x",(unsigned char)(*(line_ptr)));
                        line_ptr++;
                        remaining_bytes--;
                    }
                }
#else /* ! PRINTING_DETAILS */
                line_ptr += remaining_bytes;
#endif /* PRINTING_DETAILS */
                dwarf_printf(dbg,"\n");
                }
                break;
            } /* End switch. */
        }
    }
    block_line = (Dwarf_Line *)
        _dwarf_get_alloc(dbg, DW_DLA_LIST, line_count);
    if (block_line == NULL) {
        curr_chain = head_chain;
        /*  FIXME: chain cleanup should be a function and called at
            more places in this function.  */
        _dwarf_free_chain_entries(dbg,head_chain,line_count);
        _dwarf_error(dbg, error, DW_DLE_ALLOC_FAIL);
        return (DW_DLV_ERROR);
    }

    curr_chain = head_chain;
    for (i = 0; i < line_count; i++) {
        Dwarf_Chain t = 0;
        *(block_line + i) = curr_chain->ch_item;
        t = curr_chain;
        curr_chain = curr_chain->ch_next;
        dwarf_dealloc(dbg, t, DW_DLA_CHAIN);
    }

    if (is_single_table || !is_actuals_table) {
        line_context->lc_linebuf_logicals = block_line;
        line_context->lc_linecount_logicals = line_count;
    } else {
        line_context->lc_linebuf_actuals = block_line;
        line_context->lc_linecount_actuals = line_count;
    }
#ifdef PRINTING_DETAILS
    if (is_single_table) {
        if(!line_count) {
            dwarf_printf(dbg," Line table is present (offset 0x%"
                DW_PR_XZEROS DW_PR_DUx
                ") but no lines present\n",
                line_context->lc_section_offset);
        }
    } else if (is_actuals_table) {
        if(!line_count) {
            dwarf_printf(dbg," Line table present (offset 0x%"
                DW_PR_XZEROS DW_PR_DUx
                ") but no actuals lines present\n",
                line_context->lc_section_offset);
        }
    } else {
        if(!line_count) {
            dwarf_printf(dbg," Line table present (offset 0x%"
                DW_PR_XZEROS DW_PR_DUx
                ") but no logicals lines present\n",
                line_context->lc_section_offset);
        }
    }
#endif /* PRINTING_DETAILS */
    return DW_DLV_OK;
}
