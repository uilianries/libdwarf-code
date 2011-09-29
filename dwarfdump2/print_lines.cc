/* 
  Copyright (C) 2000-2006 Silicon Graphics, Inc.  All Rights Reserved.
  Portions Copyright 2007-2010 Sun Microsystems, Inc. All rights reserved.
  Portions Copyright 2009-2010 SN Systems Ltd. All rights reserved.
  Portions Copyright 2008-2011 David Anderson. All rights reserved.

  This program is free software; you can redistribute it and/or modify it
  under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it would be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

  Further, this software is distributed without any warranty that it is
  free of the rightful claim of any third person regarding infringement
  or the like.  Any license provided herein, whether implied or
  otherwise, applies only to this software file.  Patent licenses, if
  any, provided herein do not apply to combinations of this program with
  other software, or any other product whatsoever.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write the Free Software Foundation, Inc., 51
  Franklin Street - Fifth Floor, Boston MA 02110-1301, USA.

  Contact information:  Silicon Graphics, Inc., 1500 Crittenden Lane,
  Mountain View, CA 94043, or:

  http://www.sgi.com

  For further information regarding this notice, see:

  http://oss.sgi.com/projects/GenInfo/NoticeExplan



$Header: /plroot/cmplrs.src/v7.4.5m/.RCS/PL/dwarfdump/RCS/print_sections.c,v 1.69 2006/04/17 00:09:56 davea Exp $ */
/*  The address of the Free Software Foundation is
    Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, 
    Boston, MA 02110-1301, USA.  
    SGI has moved from the Crittenden Lane address.
*/

#include "globals.h"
#include <vector>
#include "naming.h"
#include "uri.h"
#include "dwconf.h"

#include "print_frames.h"

using std::string;
using std::cout;
using std::cerr;
using std::endl;

static void
print_source_intro(Dwarf_Die cu_die)
{
    Dwarf_Off off = 0;
    int ores = dwarf_dieoffset(cu_die, &off, &err);

    if (ores == DW_DLV_OK) {
        cout << "Source lines (from CU-DIE at .debug_info offset ";
        cout << off;
        cout << "):" << endl;
    } else {
        cout <<"Source lines (for the CU-DIE at unknown location):" <<
            endl;
    }
}

/*  Print line number information:

    filename
    new basic-block
    [line] [address] <new statement>
*/

void
print_line_numbers_this_cu(DieHolder & hcudie)
{
    Dwarf_Die cu_die = hcudie.die();
    Dwarf_Debug dbg = hcudie.dbg();

    bool InvalidSection = false;
    bool SkipRecord = false;

    error_message_data.current_section_id = DEBUG_LINE;
    if(do_print_dwarf) {
        cout << endl;
        cout << ".debug_line: line number info for a single cu"<< endl;
    }
    if (verbose > 1) {
        int errcount = 0;
        print_source_intro(cu_die);
        SrcfilesHolder hsrcfiles;
        print_one_die(hcudie, /* print_information= */ 1,
            /* indent_level= */ 0,
            hsrcfiles,
            /* ignore_die_printed_flag= */true);
        DWARF_CHECK_COUNT(lines_result,1);
        int lres = dwarf_print_lines(cu_die, &err,&errcount);
        if(errcount > 0) {
            DWARF_ERROR_COUNT(lines_result,errcount);
            DWARF_CHECK_COUNT(lines_result,(errcount-1));
        }
        if (lres == DW_DLV_ERROR) {
            print_error(dbg, "dwarf_srclines details", lres, err);
        }
        return;
    }
    if(check_lines && checking_this_compiler()) {
        DWARF_CHECK_COUNT(lines_result,1);
        int line_errs = 0;
        dwarf_check_lineheader(cu_die,&line_errs);
        if(line_errs > 0) {
            DWARF_ERROR_COUNT(lines_result,line_errs);
            DWARF_CHECK_COUNT(lines_result,(line_errs-1));
        }
    }
    Dwarf_Signed linecount = 0;
    Dwarf_Line *linebuf = NULL;
    int lres = dwarf_srclines(cu_die, &linebuf, &linecount, &err);
    if (lres == DW_DLV_ERROR) {
        /* Do not terminate processing. */
        if (check_decl_file) {
            DWARF_CHECK_COUNT(decl_file_result,1);
            DWARF_CHECK_ERROR2(decl_file_result,"dwarf_srclines",
                dwarf_errmsg(err));
            record_dwarf_error = false;  /* Clear error condition */
        } else {
            print_error(dbg, "dwarf_srclines", lres, err);
        }
    } else if (lres == DW_DLV_NO_ENTRY) {
        /* no line information is included */
    } else {
        if(do_print_dwarf) {
            print_source_intro(cu_die);
            if (verbose) {
                SrcfilesHolder hsrcfiles;
                print_one_die(hcudie, /* print_information= */ 1,
                    /* indent_level= */ 0,
                    hsrcfiles,
                    /* ignore_die_printed_flag= */true);
            }
            cout <<
                "<pc>        [row,col] NS BB ET uri: \"filepath\""
                << endl;
            cout << 
                "NS new statement, BB new basic block, ET end of text sequence"
                << endl;
        }
        string lastsrc = ""; 
        for (Dwarf_Signed i = 0; i < linecount; i++) {
            Dwarf_Line line = linebuf[i];
            char *filenamearg = 0;
            string filename("<unknown>");
            bool found_line_error = false;
            Dwarf_Bool has_is_addr_set = 0;
            string where;

             if (check_decl_file && checking_this_compiler()) {
                /* A line record with addr=0 was detected */
                if (SkipRecord) {
                    /* Skip records that do not have ís_addr_set' */
                    int ares1 = dwarf_line_is_addr_set(line, &has_is_addr_set, &err);
                    if (ares1 == DW_DLV_OK && has_is_addr_set) {
                        SkipRecord = false;
                    }
                    else {
                        /*  Keep ignoring records until we have 
                            one with 'is_addr_set' */
                        continue;
                    }
                }
            }


            int sres = dwarf_linesrc(line, &filenamearg, &err);
            if (sres == DW_DLV_ERROR) {
                print_error(dbg, "dwarf_linesrc", sres, err);
                found_line_error = true;
            }
            if (sres == DW_DLV_OK) {
                filename = filenamearg;
            }
            Dwarf_Addr pc = 0;
            int ares = dwarf_lineaddr(line, &pc, &err);
            if (ares == DW_DLV_ERROR) {
                print_error(dbg, "dwarf_lineaddr", ares, err);
                found_line_error = true;
            }
            if (ares == DW_DLV_NO_ENTRY) {
                pc = 0;
            }
            Dwarf_Unsigned lineno = 0;
            int lires = dwarf_lineno(line, &lineno, &err);
            if (lires == DW_DLV_ERROR) {
                print_error(dbg, "dwarf_lineno", lires, err);
                found_line_error = true;
            }
            if (lires == DW_DLV_NO_ENTRY) {
                lineno = -1LL;
            }
            Dwarf_Signed column = 0;
            int cores = dwarf_lineoff(line, &column, &err);
            if (cores == DW_DLV_ERROR) {
                print_error(dbg, "dwarf_lineoff", cores, err);
                found_line_error = true;
            }
            if (cores == DW_DLV_NO_ENTRY) {
                column = -1LL;
            }

            /*  Process any possible error condition, though
                we won't be at the first such error. */
            if (check_decl_file && checking_this_compiler()) {
                DWARF_CHECK_COUNT(decl_file_result,1);
                if (found_line_error) {
                    DWARF_CHECK_ERROR2(decl_file_result,where,dwarf_errmsg(err));
                } else if (do_check_dwarf) {
                    /*  Check the address lies with a valid [lowPC:highPC]
                        in the .text section*/
                    if (pAddressRangesData->IsAddressInAddressRange(pc)) {
                        /* Valid values; do nothing */
                    } else {
                        /*  At this point may be we are dealing with 
                            a linkonce symbol. The problem we have here 
                            is we have consumed the deug_info section
                            and we are dealing just with the records 
                            from the .debug_line, so no PU_name is 
                            available and no high_pc. Traverse the linkonce
                            table if try to match the pc value with 
                            one of those ranges.
                        */
                        DWARF_CHECK_COUNT(lines_result,1);
                        if (pLinkOnceData->FindLinkOnceEntry(pc)){
                            /* Valid values; do nothing */
                        } else {
                            /*  The SN Systems Linker generates 
                                line records 
                                with addr=0, when dealing with linkonce 
                                symbols and no stripping */
                            if (pc) {
                                char addr_tmp[100];
                                snprintf(addr_tmp,sizeof(addr_tmp),
                                    ".debug_line: Address"
                                    " 0x%" DW_PR_XZEROS DW_PR_DUx
                                    " outside a valid .text range",pc);
                                DWARF_CHECK_ERROR(lines_result,
                                    addr_tmp);
                            } else {
                                SkipRecord = true;
                            }
                        }
                    }
                    /*  Check the last record for the .debug_line, 
                        the one created by DW_LNE_end_sequence, 
                        is the same as the high_pc
                        address for the last known user program 
                        unit (PU) */
                    if ((i + 1 == linecount) &&
                        error_message_data.seen_PU_high_address) {
                        /*  Ignore those PU that have been stripped 
                            by the linker; their low_pc values are 
                            set to -1 (snc linker only) */
                        /*  It is perfectly sensible for a compiler
                            to leave a few bytes of NOP or other stuff
                            after the last instruction in a subprogram,
                            for cache-alignment or other purposes, so
                            a mismatch here is not necessarily 
                            an error.  */
                           
                        DWARF_CHECK_COUNT(lines_result,1);
                        if ((pc != error_message_data.PU_high_address) && 
                            (error_message_data.PU_base_address != 
                                error_message_data.elf_max_address)) {
                            char addr_tmp[100];
                            snprintf(addr_tmp,sizeof(addr_tmp),
                                ".debug_line: Address"
                                " 0x%" DW_PR_XZEROS DW_PR_DUx
                                " may be incorrect" 
                                " as DW_LNE_end_sequence address",pc);
                            DWARF_CHECK_ERROR(lines_result,
                                addr_tmp);
                        }
                    }
                }
            }

            /* Display the error information */
            if (found_line_error || record_dwarf_error) {
                if (check_verbose_mode) {
                    /* Print the record number for better error description */
                    cout << "Record = " <<
                        i << " Addr = " <<  IToHex0N(pc,10) <<
                        " [" << IToDec(lineno,4) <<
                        ","<< IToDec(column,2) <<
                        "] '" << filename << "'" << endl;
                    /* Flush due to the redirection of stderr */
                    cout.flush();
                    /* The compilation unit was already printed */
                    if (!check_decl_file) {
                        PRINT_CU_INFO();
                    }
                }
                record_dwarf_error = false; 
                InvalidSection = true;
                /* Due to a fatal error, skip current record */
                if (found_line_error) {
                    continue;
                }
            }

            cout << IToHex0N(pc,10) << "  ["  <<
                IToDec(lineno,4) << "," <<
                IToDec(column,2) <<
                "]" ;

            if (sres == DW_DLV_OK) {
                dwarf_dealloc(dbg, filenamearg, DW_DLA_STRING);
            }
            Dwarf_Bool newstatement = 0;
            int nsres = dwarf_linebeginstatement(line, &newstatement, &err);
            if (nsres == DW_DLV_OK) {
                if (newstatement) {
                    cout <<" NS";
                }
            } else if (nsres == DW_DLV_ERROR) {
                print_error(dbg, "linebeginstatment failed", nsres,
                    err);
            }
            Dwarf_Bool new_basic_block = 0;
            nsres = dwarf_lineblock(line, &new_basic_block, &err);
            if (nsres == DW_DLV_OK) {
                if (new_basic_block) {
                    cout <<" BB";
                }
            } else if (nsres == DW_DLV_ERROR) {
                print_error(dbg, "lineblock failed", nsres, err);
            }
            Dwarf_Bool lineendsequence = 0;
            nsres = dwarf_lineendsequence(line, &lineendsequence, &err);
            if (nsres == DW_DLV_OK) {
                if (lineendsequence) {
                    cout <<" ET";
                }
            } else if (nsres == DW_DLV_ERROR) {
                print_error(dbg, "lineblock failed", nsres, err);
            }
            // Here avoid so much duplication of long file paths.
            if (i > 0 && verbose < 3  && filename == lastsrc ){
                /* print no name, leave blank. */
            } else {
                string urs(" uri: \"");
                translate_to_uri(filename.c_str(),urs);
                urs.append("\"");
                if (do_print_dwarf) {
                    cout << urs ;
                }
                lastsrc = filename;
            }
            cout << endl;
        }
        dwarf_srclines_dealloc(dbg, linebuf, linecount);
    }
}

