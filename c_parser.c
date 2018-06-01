#include <stdio.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <sys/dir.h>

//#include <error.h>
#include <tables_dict.h>
#include <print_data.h>
#include <check_data.h>
#include <dirent.h>

#include <decimal.h>
#include <m_ctype.h>
//#include <decimal.c>
//#include <ctype-latin1.c>
//#include <ctype-utf8.c>
//#include <my_strtoll10.c>
//#include <strend.c>
//#include <ctype-simple.c>
//#include <my_vsnprintf.c>

#include <bchange.c>
#include <bcmp.c>
#include <bfill.c>
#include <bmove512.c>
#include <bmove.c>
#include <bmove_upp.c>
#include <ctype-bin.c>
#include <ctype.c>
#include <ctype-latin1.c>
#include <ctype-mb.c>
#include <ctype-simple.c>
#include <ctype-utf8.c>
#include <decimal.c>
#include <int2str.c>
#include <is_prefix.c>
#include <llstr.c>
#include <longlong2str.c>
#include <my_strtoll10.c>
#include <my_vsnprintf.c>
#include <r_strinstr.c>
#include <str2int.c>
#include <str_alloc.c>
#include <strappend.c>
#include <strcend.c>
#include <strcont.c>
#include <strend.c>
#include <strfill.c>
#include <strinstr.c>
#include <strmake.c>
#include <strmov.c>
#include <strnlen.c>
#include <strnmov.c>
#include <strstr.c>
#include <strtod.c>
#include <strtol.c>
#include <strtoll.c>
#include <strtoul.c>
#include <strtoull.c>
#include <strxmov.c>
#include <strxnmov.c>
#include <xml.c>


#define DIG_MASK     100000000
#define DIG_MAX      (DIG_BASE-1)

#define DIG_BASE     1000000000
#define FILES_IN_DIR  1024


// Global flags from getopt
bool deleted_pages_only = 0;
bool deleted_records_only = 0;
bool undeleted_records_only = 1;
bool brute_force = 0;
bool debug = 0;
//bool process_redundant = 0;
//bool process_compact = 0;
bool process_56 = 0;
char blob_dir[256] = ".";
char dump_prefix[256] = "default";
char path_ibdata[256];
bool external_in_ibdata = 0;

dulint filter_id;
int use_filter_id = 0;

FILE* f_result;
FILE* f_sql;

extern int load_table(char*);

inline void error(char *msg) {
  fprintf(stderr, "Error: %s\n", msg);
  exit(1);
}

/* Recovery status counter */
unsigned long records_expected_total = 0;
unsigned long records_dumped_total = 0;
int records_lost = 0;

/*****************************************************************
 * Prints the contents of a memory buffer in hex and ascii. */

void
ut_print_buf(
/*=========*/
    FILE*       file,   /* in: file where to print */
    const byte* buf,    /* in: memory buffer */
    ulint       len)    /* in: length of the buffer */
{
    const byte* data;
    ulint       i;

    fprintf(file, " len %lu; hex ", len);

    for (data = buf, i = 0; i < len; i++) {
        fprintf(file, "%02lx", (ulong)*data++);
    }

    fputs("; asc ", file);

    data = buf;

    for (i = 0; i < len; i++) {
        int c = (int) *data++;
        putc(isprint(c) ? c : ' ', file);
    }

    putc(';', file);
}

/*******************************************************************/
ulint process_ibrec(page_t *page, rec_t *rec, table_def_t *table, ulint *offsets) {
	ulint data_size;
	int i;
	// Print trx_id and rollback pointer
	for(i = 0; i < table->fields_count; i++) {
		ulint len;
		byte *field = rec_get_nth_field(rec, offsets, i, &len);

		if (table->fields[i].type == FT_INTERNAL) {
			if (debug) {
                printf("Field #%i @ %p: length %lu, value: ", i, field, len);
            }

			print_field_value(field, len, &(table->fields[i]));
			
            if (i < table->fields_count - 1) {
                fprintf(f_result, "\t");
            }
			if (debug) {
                printf("\n");
            }
		}
	}

	// Print table name
	if (debug) {
		printf("Processing record %p from table '%s'\n", rec, table->name);
		rec_print_new(stdout, rec, offsets);
	} else {
		fprintf(f_result, "%s\t", table->name);
	}

	data_size = rec_offs_data_size(offsets);

	for(i = 0; i < table->fields_count; i++) {
		ulint len;
		byte *field = rec_get_nth_field(rec, offsets, i, &len);

		if (table->fields[i].type == FT_INTERNAL) continue;

		if (debug) printf("Field #%i @ %p: length %lu, value: ", i, field, len);

		if (len == UNIV_SQL_NULL) {
			fprintf(f_result, "NULL");
		} else {
            if (rec_offs_nth_extern(offsets, i)) {
                print_field_value_with_external(field, len, &(table->fields[i]));
            } else {
			      print_field_value(field, len, &(table->fields[i]));
            }
		}

		if (i < table->fields_count - 1) fprintf(f_result, "\t");
		if (debug) printf("\n");
	}
	fprintf(f_result, "\n");
	return data_size; // point to the next possible record's start
}

/*******************************************************************/
inline ibool check_constraints(rec_t *rec, table_def_t* table, ulint* offsets) {
	int i;
    ulint len_sum = 0;

	if (debug) {
		printf("\nChecking constraints for a row (%s) at %p:", table->name, rec);
		ut_print_buf(stdout, rec, 100);
	}

	// Check every field
	for(i = 0; i < table->fields_count; i++) {
		// Get field value pointer and field length
		ulint len;
		byte *field = rec_get_nth_field(rec, offsets, i, &len);
		if (debug) printf("\n - field %s(addr = %p, len = %lu):", table->fields[i].name, field, len);

        if (len != UNIV_SQL_NULL) {
            len_sum += len;
        } else {
            if (!rec_offs_comp(offsets)) {
                len_sum += rec_get_nth_field_size(rec, i);
            }
        }


		// Skip null fields from type checks and fail if null is not allowed by data limits
		if (len == UNIV_SQL_NULL) {
			if (table->fields[i].has_limits && !table->fields[i].limits.can_be_null) {
				if (debug) {
                    printf("data can't be NULL");
                }
				return FALSE;
			}
			continue;
		}

		// Check limits
		if (!table->fields[i].has_limits) {
            continue;
        }
		if (!check_field_limits(&(table->fields[i]), field, len)) {
			if (debug) {
                printf("LIMITS check failed(field = %p, len = %ld)!\n", field, len);
            }
			return FALSE;
		}
	}

    // Why do we need this check?
    /*
    if (len_sum != rec_offs_data_size(offsets)) {
        fprintf(stderr,
            "\nInnoDB: Error: record len should be %lu, len %lu\n",
            (ulong) len_sum,
            (ulong) rec_offs_data_size(offsets));
            return FALSE;
            }
    */

	if (debug) {
        printf("\nRow looks OK!\n");
    }
	return TRUE;
}

/*******************************************************************/
inline ibool check_fields_sizes(rec_t *rec, table_def_t *table, ulint *offsets) {
	int i;

	if (debug) {
		printf("\nChecking field lengths for a row (%s): ", table->name);
		printf("OFFSETS: ");
		unsigned long int prev_offset = 0;
		unsigned long int curr_offset = 0;
		for(i = 0; i < rec_offs_n_fields(offsets); i++) {
			curr_offset = rec_offs_base(offsets)[i];
			printf("%lu (+%lu); ", curr_offset, curr_offset - prev_offset);
			prev_offset = curr_offset;
		}
//		printf("\n");
	}

	// check every field
	for(i = 0; i < table->fields_count; i++) {
		// Get field size
		ulint len = rec_offs_nth_size(offsets, i);
		if (debug) {
            printf("\n - field %s(%lu):", table->fields[i].name, len);
        }
		// If field is null
		if (len == UNIV_SQL_NULL) {
			// Check if it can be null and jump to a next field if it is OK
			if (table->fields[i].can_be_null) {
                continue;
            }
			// Invalid record where non-nullable field is NULL
			if (debug) {
                printf("Can't be NULL or zero-length!\n");
            }
            return FALSE;
		}

		// Check size of fixed-length field
		if (table->fields[i].fixed_length) {
			// Check if size is the same and jump to the next field if it is OK
			if (len == table->fields[i].fixed_length || (len == 0 && table->fields[i].can_be_null)) {
                continue;
            }
			// Invalid fixed length field
			if (debug) {
                printf("Invalid fixed length field size: %lu, but should be %u!\n", len, table->fields[i].fixed_length);
            }
            return FALSE;
		}

		// Check if has externally stored data
		if (rec_offs_nth_extern(offsets, i)) {
			if (debug) {
                printf("\nEXTERNALLY STORED VALUE FOUND in field %i\n", i);
            }
            if (table->fields[i].type == FT_TEXT || table->fields[i].type == FT_BLOB) {
                continue;
            }
			if (debug) {
                printf("Invalid external data flag!\n");
            }
			return FALSE;
		}

		// Check size limits for varlen fields
		if (len < table->fields[i].min_length || len > table->fields[i].max_length) {
			if (debug) {
                printf("Length limits check failed (%lu < %u || %lu > %u)!\n", len, table->fields[i].min_length, len, table->fields[i].max_length);
            }
			return FALSE;
		}

		if (debug) {
            printf("OK!");
        }
	}

	if (debug) {
        printf("\n");
    }
	return TRUE;
}

/*******************************************************************/
inline ibool ibrec_init_offsets_new(page_t *page, rec_t* rec, table_def_t* table, ulint* offsets) {
	ulint i = 0;
	ulint offs;
	const byte* nulls;
	const byte* lens;
	ulint null_mask;
	ulint status = rec_get_status(rec);

	// Skip non-ordinary records
	if (status != REC_STATUS_ORDINARY) {
        return FALSE;
    }
	// First field is 0 bytes from origin point
	rec_offs_base(offsets)[0] = 0;

	// Init first bytes
	rec_offs_set_n_fields(offsets, table->fields_count);

	nulls = rec - (REC_N_NEW_EXTRA_BYTES + 1);
	lens = nulls - (table->n_nullable + 7) / 8;
	offs = 0;
	null_mask = 1;

	/* read the lengths of fields 0..n */
	do {
		ulint	len;
		field_def_t *field = &(table->fields[i]);

		/* nullable field => read the null flag */
		if (field->can_be_null) {
//			if (debug) printf("nullable field => read the null flag\n");
			if (!(byte)null_mask) {
				nulls--;
				null_mask = 1;
			}

			if (*nulls & null_mask) {
				null_mask <<= 1;
				/* No length is stored for NULL fields.
				We do not advance offs, and we set
				the length to zero and enable the
				SQL NULL flag in offsets[]. */
				len = offs | REC_OFFS_SQL_NULL;
				goto resolved;
			}
			null_mask <<= 1;
		}

		if (!field->fixed_length) {
//			if (debug) printf("Variable-length field: read the length\n");
			/* Variable-length field: read the length */
			len = *lens--;

			if (field->max_length > 255 || field->type == FT_BLOB || field->type == FT_TEXT) {
				if (len & 0x80) {
					/* 1exxxxxxx xxxxxxxx */
					len <<= 8;
					len |= *lens--;

					offs += len & 0x3fff;
					if (len	& 0x4000) {
						len = offs | REC_OFFS_EXTERNAL;
					} else {
						len = offs;
					}

					goto resolved;
				}
			}

			len = offs += len;
		} else {
			len = offs += field->fixed_length;
		}
	resolved:
        offs &= 0xffff;
		if (rec + offs - page > UNIV_PAGE_SIZE) {
			if (debug) {
                printf("Invalid offset for field %lu: %lu\n", i, offs);
            }
			return FALSE;
		}
		rec_offs_base(offsets)[i + 1] = len;
	} while (++i < table->fields_count);

	return TRUE;
}

/*******************************************************************/
inline ibool ibrec_init_offsets_old(page_t *page, rec_t* rec, table_def_t* table, ulint* offsets) {
	ulint i = 0;
	ulint offs;

	// First field is 0 bytes from origin point
	rec_offs_base(offsets)[0] = 0;

	// Init first bytes
	rec_offs_set_n_fields(offsets, table->fields_count);

	/* Old-style record: determine extra size and end offsets */
	offs = REC_N_OLD_EXTRA_BYTES;
	if (rec_get_1byte_offs_flag(rec)) {
		offs += rec_offs_n_fields(offsets);
		*rec_offs_base(offsets) = offs;
		/* Determine offsets to fields */
		do {
			offs = rec_1_get_field_end_info(rec, i);
			if (offs & REC_1BYTE_SQL_NULL_MASK) {
				offs &= ~REC_1BYTE_SQL_NULL_MASK;
				offs |= REC_OFFS_SQL_NULL;
			}

            offs &= 0xffff;
    		if (rec + offs - page > UNIV_PAGE_SIZE) {
    			if (debug) printf("Invalid offset for field %lu: %lu\n", i, offs);
    			return FALSE;
    		}

			rec_offs_base(offsets)[1 + i] = offs;
		} while (++i < rec_offs_n_fields(offsets));
	} else {
		offs += 2 * rec_offs_n_fields(offsets);
		*rec_offs_base(offsets) = offs;
		/* Determine offsets to fields */
		do {
			offs = rec_2_get_field_end_info(rec, i);
			if (offs & REC_2BYTE_SQL_NULL_MASK) {
				offs &= ~REC_2BYTE_SQL_NULL_MASK;
				offs |= REC_OFFS_SQL_NULL;
			}

			if (offs & REC_2BYTE_EXTERN_MASK) {
				offs &= ~REC_2BYTE_EXTERN_MASK;
				offs |= REC_OFFS_EXTERNAL;
			}

            offs &= 0xffff;
    		if (rec + offs - page > UNIV_PAGE_SIZE) {
    			if (debug) printf("Invalid offset for field %lu: %lu\n", i, offs);
    			return FALSE;
    		}

			rec_offs_base(offsets)[1 + i] = offs;
		} while (++i < rec_offs_n_fields(offsets));
	}

	return TRUE;
}

/*******************************************************************/
inline ibool check_for_a_record(page_t *page, rec_t *rec, table_def_t *table, ulint *offsets) {
	ulint offset, data_size;
    int flag;

	// Check if given origin is valid
	offset = rec - page;
	if (offset < record_extra_bytes + table->min_rec_header_len) {
        return FALSE;
    }
	if (debug) {
        printf("ORIGIN=OK ");
    }

    flag = rec_get_deleted_flag(rec, page_is_comp(page));
    
    if (debug) {
        printf("DELETED=0x%X ", flag);
    }
	// Skip non-deleted records
	if (deleted_records_only && flag == 0) {
        return FALSE;
    }

	// Skip deleted records
	if (undeleted_records_only && flag != 0) {
        return FALSE;
    }

    // Get field offsets for current table
	int comp = page_is_comp(page);
	if (comp && !ibrec_init_offsets_new(page, rec, table, offsets)) {
        return FALSE;
    }
	if (!comp && !ibrec_init_offsets_old(page, rec, table, offsets)) {
        return FALSE;
    }
	if (debug) {
        printf("OFFSETS=OK ");
    }

	// Check the record's data size
	data_size = rec_offs_data_size(offsets);
	if (data_size > table->data_max_size) {
        if (debug) {
            printf("DATA_SIZE=FAIL(%lu > %ld) ", (long int)data_size, (long int)table->data_max_size);
        }
        return FALSE;
	}
	if (data_size < table->data_min_size) {
        if (debug) {
            printf("DATA_SIZE=FAIL(%lu < %lu) ", (long int)data_size, (long int)table->data_min_size);
        }
        return FALSE;
	}
	if (debug) {
        printf("DATA_SIZE=OK ");
    }
	// Check fields sizes
	if (!check_fields_sizes(rec, table, offsets)) {
        return FALSE;
    }
	if (debug) {
        printf("FIELD_SIZES=OK ");
    }
	// This record could be valid and useful for us
	return TRUE;
}

/*******************************************************************/
int check_page(page_t *page, unsigned int *n_records) {
    int comp = page_is_comp(page);
    int16_t i, s, p, b, p_prev;
    int recs = 0;
    int max_recs = UNIV_PAGE_SIZE / 5;
    *n_records = 0;
    i = (comp) ? PAGE_NEW_INFIMUM : PAGE_OLD_INFIMUM;
    s = (comp) ? PAGE_NEW_SUPREMUM : PAGE_OLD_SUPREMUM;
/**originaly, we scan all the records from the offset (100 + 5 / 6) to find deleted records,
 * which may get in trouble in the real test.
 * And now, we do it in another way.
 * with deleted_records_only == 1, we still check if the page is valid.
	if(deleted_records_only == 1) {
		if (debug) {
            printf("We look for deleted records only. Consider all pages are not valid\n");
        }
		return 0;
	}
**/
	if (brute_force == 1) {
		if (debug) {
            printf("Scan from the first record(brute force), Consider all pages are not valid\n");
        }
		return 0;
	}
    if (debug) {
        printf("Checking a page\nInfimum offset: 0x%X\nSupremum offset: 0x%X\n", i, s);
    }
    p_prev = 0;
    p = i;
    while (p != s) {
        if (recs > max_recs) {
            *n_records = 0;
            if (debug) {
                printf("Page is bad\n");
            }
            return 0;
        }
        // If a pointer to the next record is negative - the page is bad
        if (p < 2) {
            *n_records = 0;
            if (debug) {
                printf("Page is bad\n");
            }
            return 0;
        }
	// If the pointer is bigger than UNIV_PAGE_SIZE, the page is corrupted
        if (p > UNIV_PAGE_SIZE) {
            *n_records = 0;
            if (debug) {
                printf("Page is bad\n");
            }
            return 0;
        }
	//  If we've already was here, the page is bad
        if (p == p_prev) {
            *n_records = 0;
            if (debug) printf("Page is bad\n");
            return 0;
        }
	    p_prev = p;
        // Get next pointer
        if (comp) {
            b = mach_read_from_2(page + p - 2);
            p = p + b;
        } else {
            p =  mach_read_from_2(page + p - 2);
        }
        if (debug) {
            printf("Next record at offset: 0x%X (%d) \n", 0x0000FFFF & p, p);
        }

        recs++;
    }
    *n_records = recs -1; // - infinum record
    if (debug) {
        printf("Page is good\n");
    }
    return 1;
}
/*******************************************************************/
unsigned int variable_lengthlist_len(table_def_t* table) {
    unsigned int count = 0;
    int i = 0;
    while (i < table->fields_count) {
      if(!table->fields[i].can_be_null && table->fields[i].fixed_length == 0) {
          // the following if statement may be a bug in some condition
          // can not find the best way right now
          // because can not get the actural length of any nonfixed length
          if (table->fields[i].max_length > 255 && table->fields[i].min_length > 127) {
              count += 2;
          } else {
            ++count;
          }
      }
      i++;
    }
    return count;
}

void process_ibpage(page_t *page) {
    ulint page_id;
	rec_t *origin;
	ulint offsets[MAX_TABLE_FIELDS + 2];
	ulint offset, i;
    int is_page_valid = 0;
    int comp;
    unsigned int expected_records = 0;
    unsigned int expected_records_inheader = 0;
    unsigned int actual_records = 0;
    int16_t infimum, supremum, b = 1;
    int null_columns = 0;
    int count_zero_b = 0;
    unsigned int variable_length = 0, prefix_header_length = 0;

	// Skip tables if filter used
    if (use_filter_id) {
        dulint index_id = mach_read_from_8(page + PAGE_HEADER + PAGE_INDEX_ID);
        if (index_id.low != filter_id.low || index_id.high != filter_id.high) {
            if (debug) {
            	page_id = mach_read_from_4(page + FIL_PAGE_OFFSET);
                printf("Skipped using index id filter: %lu!\n", page_id);
            }
            return;
        }
    }

	// Read page id
	page_id = mach_read_from_4(page + FIL_PAGE_OFFSET);
	if (debug) printf("Page id: %lu\n", page_id);
	fprintf(f_result, "-- Page id: %lu", page_id);

	if (table_definitions_cnt == 0) {
	    fprintf(stderr, "There are no table definitions. Please check  include/table_defs.h\n");
		exit(EXIT_FAILURE);
	}
    is_page_valid = check_page(page, &expected_records);

    // comp == 1 if page in COMPACT format and 0 if REDUNDANT
    comp = page_is_comp(page);
    fprintf(f_result, ", Format: %s", (comp ) ? "COMPACT": "REDUNDANT");
    infimum = (comp) ? PAGE_NEW_INFIMUM : PAGE_OLD_INFIMUM;
    supremum = (comp) ? PAGE_NEW_SUPREMUM : PAGE_OLD_SUPREMUM;
	
    /**if undeleted_records_only, we scan the records from the infimum's next
     * record offset;
     * if deleted_records_only, we scan the records from the PAGE_FREE pointer.
     * if you want to recover the last (or more) deleted records,
     * you must make sure that the you didn't insert anything after you have deleted
     * something if you want to recover what you have deleted
     * */
	if (is_page_valid && undeleted_records_only == 1) {
        b = mach_read_from_2(page + infimum - 2);
        offset = (comp) ? infimum + b : b;
    } else if (is_page_valid && deleted_records_only == 1) {
        offset = page_header_get_field(page, PAGE_FREE);
    } else {
    // Find possible data area start point (at least 5 bytes of utility data)
    // (the original offset is proved to be wrong in real test in COMPACT FORMAT)
        // just process one table at the same time
        if (table_definitions_cnt == 0) {
            if (debug) {
              printf("no table found and table_definitions_cnt ==0\n");
            }
            exit(-1);
        } 
        table_def_t *table = &(table_definitions[0]);
        null_columns = table->n_nullable; 
        variable_length = variable_lengthlist_len(table);
        prefix_header_length = variable_length + (null_columns + 8 - 1) / 8;
        offset = 120 + record_extra_bytes + prefix_header_length;

    }
    	fprintf(f_result, ", Records list: %s", is_page_valid? "Valid": "Invalid");
        expected_records_inheader = mach_read_from_2(page + PAGE_HEADER + PAGE_N_RECS);
    	fprintf(f_result, ", Expected records: (%u %u)", expected_records, expected_records_inheader);
    	fprintf(f_result, "\n");
	if (debug) {
        printf("Starting offset: %lu (%lX). Checking %d table definitions.\n", offset, offset, table_definitions_cnt);
    }

    if (debug) {
        printf("offset < UNIV_PAGE_SIZE - record_extra_bytes: %lu < %d - %d \n", offset,  UNIV_PAGE_SIZE,  record_extra_bytes);
        printf("is_page_valid: %d\n", is_page_valid);
        printf("b: %d\n", b);
        printf("count_zero_b: %d\n", count_zero_b);
        printf("supremum: %d\n", supremum);
    }

	// Walk through all possible positions to the end of page
	// (start of directory - extra bytes of the last rec)
    //is_page_valid = 0;
	while (offset < UNIV_PAGE_SIZE - record_extra_bytes 
            && ((is_page_valid && b != 0) || (!is_page_valid && count_zero_b < 2)) 
            && ((offset != supremum ) || !is_page_valid)) {
		// Get record pointer
		origin = page + offset;
		if (debug) {
            printf("\nChecking offset: 0x%lX: ", offset);
        }

		// Check all tables
		for (i = 0; i < table_definitions_cnt; i++) {
			// Get table info
			table_def_t *table = &(table_definitions[i]);
			if (debug) {
                printf(" (%s) ", table->name);
            }
			// Check if origin points to a valid record
			if (check_for_a_record(page, origin, table, offsets) && check_constraints(origin, table, offsets)) {
				actual_records++;
				if (debug) {
                    printf("\n---------------------------------------------------\n"
			       			  "PAGE%lu: Found a table %s record: %p (offset = %lu)\n", \
						  page_id, table->name, origin, offset);
                }
                if (is_page_valid) {
					process_ibrec(page, origin, table, offsets);
                    b = mach_read_from_2(page + offset - 2);
					offset = (comp) ? offset + b : b;
                } else {
                    // if b equals to 0 for more than one time, it's the end of
                    // the data field;
                    b = mach_read_from_2(page + offset - 2);
                    if (b == 0) {
                      count_zero_b++;
                    }
					
                    offset += process_ibrec(page, origin, table, offsets); //plus data_size
                    // plus next record's variable_list length & header length
                    // may be a bug in some condition
                    // TODO how to fix this potential bug
                    offset += (record_extra_bytes + prefix_header_length);  
                }
                if (debug) {
                    printf("Next offset: 0x%lX", offset);
                }
                break;
		    } else {
                if (is_page_valid) {
					b = mach_read_from_2(page + offset - 2);
					offset = (comp) ? offset + b : b;
                } else {
					offset++;
				}
                if (debug) {
                    printf("\nNext offset: %lX", offset);
                }
            }
		}
	}
	fflush(f_result);
	int leaf_page = mach_read_from_2(page + PAGE_HEADER + PAGE_LEVEL) == 0;
	int lost_records = (actual_records != expected_records) && (actual_records != expected_records_inheader);
	fprintf(f_result, "-- Page id: %lu", page_id);
	fprintf(f_result, ", Found records: %u", actual_records);
	fprintf(f_result, ", Lost records: %s", lost_records ? "YES": "NO");
	fprintf(f_result, ", Leaf page: %s", leaf_page ? "YES": "NO");
	fprintf(f_result, "\n");
	if (leaf_page) {
	    records_expected_total += expected_records_inheader;
	    records_dumped_total += actual_records;
	    if (lost_records) {
	        records_lost = 1;
	    }
	}
}

/*******************************************************************/
void process_ibfile(int fn) {
	int read_bytes;
	page_t *page = malloc(UNIV_PAGE_SIZE);
    struct stat st;
    off_t pos;
    ulint free_offset;

	if (!page) {
        fprintf(stderr, "Can't allocate page buffer!");
        exit(EXIT_FAILURE);
        }


	if (debug) printf("Read data from fn=%d...\n", fn);

    // Get file info
    fstat(fn, &st);

	// Read pages to the end of file
	while ((read_bytes = read(fn, page, UNIV_PAGE_SIZE)) == UNIV_PAGE_SIZE) {
        pos = lseek(fn, 0, SEEK_CUR);

        if (pos % (UNIV_PAGE_SIZE * 512) == 0) {
            fprintf(f_sql, "-- %.2f%% done\n", 100.0 * pos / st.st_size);
        }

	    if (deleted_pages_only) {
    		free_offset = page_header_get_field(page, PAGE_FREE);
    		if (page_header_get_field(page, PAGE_N_RECS) == 0 && free_offset == 0) continue;
    		if (free_offset > 0 && page_header_get_field(page, PAGE_GARBAGE) == 0) continue;
    		if (free_offset > UNIV_PAGE_SIZE) continue;
    	}

        // Initialize table definitions (count nullable fields, data sizes, etc)
        init_table_defs(page_is_comp(page));
        process_ibpage(page);

	}
	free(page);
}

/*******************************************************************/
int open_ibfile(char *fname) {
	struct stat fstat;
	int fn;

	// Skip non-regular files
	if (debug) {
        printf("Opening file: %s\n", fname);
    }
	if (stat(fname, &fstat) != 0 || (fstat.st_mode & S_IFREG) != S_IFREG) {
        fprintf(stderr, "Invalid file specified!");
        exit(EXIT_FAILURE);
    }
	fn = open(fname, O_RDONLY, 0);
	if (!fn) {
        fprintf(stderr, "Can't open file!");
        exit(EXIT_FAILURE);
    }
	return fn;
}

/*******************************************************************/
void set_filter_id(char *id) {
    int cnt = sscanf(id, "%lu:%lu", &filter_id.high, &filter_id.low);
    if (cnt < 2) {
        fprintf(stderr, "Invalid index id provided! It should be in N:M format, where N and M are unsigned integers");
        exit(EXIT_FAILURE);
    }
    use_filter_id = 1;
}

/*******************************************************************/
void usage() {
	error(
//	  "Usage: ./c_parser [dDA] -f <InnoDB page or dir> -t table.sql\n"
	  "  Where\n"
	//  "    -f <InnoDB page(s)> -- InnoDB page or directory with pages(all pages should have same index_id)\n"
	  "    -i <table ibdfiles directory> \n"
      "    -t <table.sql directory> -- CREATE statement of a table\n"
	  "    -o <output directory> -- Save dump in this directory. Otherwise print to stdout\n"
	  "    -l <output directory> -- Save  pages in this folder\n"
	  "    -h  -- Print this help\n"
      "    -c  -- whether delete page files or not after the recovery\n" 
   // "    -d  -- Process only those pages which potentially could have deleted records (default = NO)\n"
	  "    -D  -- Recover deleted rows only (default = NO)\n"
	  "    -U  -- Recover UNdeleted rows only (default = YES)\n"
      "    -A  -- Recover All rows in brute force way (default = NO)\n"
  //  "    -V  -- Verbose mode (lots of debug information)\n"
  //  "    -4  -- innodb_datafile is in REDUNDANT format\n"
//	  "    -5  -- innodb_datafile is in COMPACT format\n"
  //  "    -6  -- innodb_datafile is in MySQL 5.6 format\n"
  //  "    c_parser can detect REDUNDANT or COMPACT, so -4 and -5 are optional. If you use MySQL 5.6+ however, -6 is necessary\n"
  //  "    -T  -- retrieves only pages with index id = NM (N - high word, M - low word of id)\n"
  //  "    -b <dir> -- Directory where external pages can be found. Usually it is pages-XXX/FIL_PAGE_TYPE_BLOB/\n"
  //  "    -i <file> -- Read external pages at their offsets from <file>.\n"
  //  "    -p prefix -- Use prefix for a directory name in LOAD DATA INFILE command\n"
	  "\n"
	);
}

extern int get_primary_page(char*, char*);
extern int dirlist(char* in, char* arg1[], char* arg2[]);
extern int stream_parser(char*, char*);
extern int rm_dir(char* page_dir);
/*******************************************************************/
int main(int argc, char **argv) {
	int fn = 0, ch, no = 0, num_of_ibfs = 0, i = 0;
	int delete = 1; // if delete pages
	struct stat st;
	char src[350] = "";
    char tmp[256] = "";
    char ibfile_arg[256]= "";
    char page_arg[256] = "";
	char table_schema[256] = "";
    char result_file_dir[256] = "";

    char* ibfiles[FILES_IN_DIR];
    char* tablenames[FILES_IN_DIR];
	char* primary_page[FILES_IN_DIR];
    char buffer[BUFSIZ];
    

    setvbuf(stdout, buffer, _IOFBF, sizeof(buffer));

	f_result = stdout;
	f_sql = stderr;
	char result_file[1024];
	char sql_file[1024];
	while ((ch = getopt(argc, argv, "t:456hdADUVcT:b:p:o:i:l:")) != -1) {
		switch (ch) {
			case 'd':
				//deleted_pages_only = 1;
				break;
			case 'D':
			    deleted_records_only = 1;
			    undeleted_records_only = 0;
				break;
			case 'U':
                deleted_records_only = 0;
			    undeleted_records_only = 1;
				break;
            case 'A':
                deleted_records_only = 0;
                undeleted_records_only = 0;
                brute_force = 1;
                break;
			case 'o':
				strncpy(result_file_dir, optarg, sizeof(result_file_dir));
				break;
			case 'i':
                strncpy(ibfile_arg, optarg, sizeof(ibfile_arg));
				//strncpy(path_ibdata, optarg, sizeof(path_ibdata));
                //external_in_ibdata = 1;
				break;
            case 'l':
			    strncpy(page_arg, optarg, sizeof(page_arg));
                /*
                strncpy(sql_file_dir, optarg, sizeof(sql_file));
                if(NULL == (f_sql = fopen(sql_file, "w"))){
                    fprintf(stderr, "Can't open file %s for writing\n", sql_file);
                    exit(-1);
                }
                */
                break;

            case 'c':
                delete = 0;
                break;
			case 't':
			    strncpy(table_schema, optarg, sizeof(table_schema));
				break;
			case 'f':
                /*
				strncpy(src, optarg, sizeof(src));
				if(stat(src, &st) == 0){
					if(S_ISDIR(st.st_mode)){
						is_dir = 1;
					}
				} else {
					perror("stat");
					fprintf(stderr, "Can't stat %s\n", src);
					exit(-1);
				}
                */
				break;
			case 'V':
				debug = 1;
				break;
            case '4':
            case '5':
                break;
            case '6':
                process_56 = 1;
                break;
            /*
            case 'T':
                set_filter_id(optarg);
                break;
            case 'b':
                strncpy(blob_dir, optarg, sizeof(blob_dir));
                break;
			case 'p':
				strncpy(dump_prefix, optarg, sizeof(dump_prefix));
				break;
            **/    
			default:
			case '?':
			case 'h':
				usage();
		}
	}
    /*
    printf("-----your create_sql dir: %s\n", table_schema);
    printf("-----your pages dir: %s\n", page_arg);
    printf("-----your output dir: %s\n", result_file_dir);
    printf("-----your idbfile dir: %s\n", ibfile_arg);
    */
    for (i = 0; i < FILES_IN_DIR; i++) {
        ibfiles[i] = (char*)malloc(sizeof(char) * 256);
        tablenames[i] = (char*)malloc(sizeof(char) * 64);
        primary_page[i] = (char*)malloc(sizeof(char) * 350);
        memset(ibfiles[i], '\0', 256);
        memset(tablenames[i], '\0', 64);
        memset(primary_page[i], '\0', 350);
    }

    if (stat(ibfile_arg, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            num_of_ibfs = dirlist(ibfile_arg, ibfiles, tablenames);
            for (no = 0; no < num_of_ibfs; no++) {
                memset(tmp, '\0', sizeof(tmp));
                strcat(tmp, page_arg);
                strcat(tmp, "/pages-");
                strcat(tmp, tablenames[no]);
                stream_parser(ibfiles[no], tmp);
                strcat(tmp, "/FIL_PAGE_INDEX");
                get_primary_page(tmp, primary_page[no]);
            }

        } else {
            printf("please provide a directory with ibd files\n");
            exit(-1);
            num_of_ibfs = 1;
            strcpy(ibfiles[0], ibfile_arg);
            //here read the tablenames[0]
            memset(tmp, '\0', sizeof(tmp));
            strcpy(tmp, page_arg);
            strcat(tmp, "/pages-");
            strcat(tmp, tablenames[0]);
            stream_parser(ibfile_arg, tmp);
            strcat(tmp, "/FIL_PAGE_INDEX");
            get_primary_page(tmp, primary_page[0]);
        }
    } else {
		perror("stat");
		printf( "Can't stat ibfile_arg %s\n", ibfile_arg);
		exit(-1);
    }
    //stream_parser process over;
	printf( "!!!!!!!!!process ibd files over!!!!!!!\n");
    /*
    for (i = 0; i < num_of_ibfs; i++) {
        printf("%d:  %s**%s**%s\n", i+1, ibfiles[i], tablenames[i], primary_page[i]);
    }
    */
    //exit(0);

    for (no = 0; no < num_of_ibfs; no++) {
        char page_dir[256] = "";
        char table_temp[256] = "";
        strcpy(page_dir, page_arg);
        strcat(page_dir, "/pages-");
        strcat(page_dir, tablenames[no]);  // just for delete later
        
        strncpy(table_temp, table_schema, sizeof(table_temp));
        strcat(table_temp, "/");
        strcat(table_temp, tablenames[no]);
        strcat(table_temp, ".sql");
        memset(src, '\0', sizeof(src));
        strncpy(src, primary_page[no], strlen(primary_page[no]));

        printf("process table structure for: %s : %s\n", table_temp, tablenames[no]);

        if (load_table(table_temp) != 0) {
            printf("Failed to parse table structure\n");
            usage();
            exit(EXIT_FAILURE);
        }
    
		if (0 == (fn = open_ibfile(src))) {
			fprintf(stderr, "Can't open %s\n", src);
			perror("open_ibfile");
			exit(-1);
		}

        memset(result_file, '\0', sizeof(result_file));
        strcpy(result_file, result_file_dir);
        strcat(result_file, "/");
        strcat(result_file, tablenames[no]);
          
		if(NULL == (f_result = fopen(result_file, "w"))){
			fprintf(stderr, "Can't open file %s for writing\n", result_file);
			exit(-1);
		}

		process_ibfile(fn);
		close(fn);
        
        memset(sql_file, '\0', sizeof(sql_file));
        strcpy(sql_file, result_file_dir);
        strcat(sql_file, "/load_");
        strcat(sql_file, tablenames[no]);
        strcat(sql_file, ".sql");

        if(NULL == (f_sql = fopen(sql_file, "w"))){
            fprintf(stderr, "Can't open file %s for writing\n", sql_file);
            exit(-1);
        }

	    table_def_t *table = &(table_definitions[0]);
	    fprintf(f_sql, "SET FOREIGN_KEY_CHECKS=0;\n");
	    fprintf(f_sql, "LOAD DATA LOCAL INFILE '");
	    if (f_result == stdout) {
		    fprintf(f_sql, "%s/dumps/%s/%s", getenv("PWD"), dump_prefix, table->name);
	    } else {
		    fprintf(f_sql, "%s", result_file);
	    }
	    fprintf(f_sql, "' REPLACE INTO TABLE `%s` FIELDS TERMINATED BY '\\t' OPTIONALLY ENCLOSED BY '\"' LINES STARTING BY '%s\\t' ", table->name, table->name);
	    int i = 0;
	    int comma = 0;
	    int has_set = 0;
	    fprintf(f_sql, "(");
	    for(i = 0; i < table->fields_count; i++) {
		    if(table->fields[i].type == FT_INTERNAL) {
              continue;
            }
		    if(comma) {
                fprintf(f_sql, ", ");
            }
		  switch (table->fields[i].type) {
			    case FT_BLOB:
			    case FT_BIN:
				    fprintf(f_sql, "@var_%s", table->fields[i].name);
				    has_set = 1;
				    break;
			    case FT_BIT:
				    fprintf(f_sql, "@var_%s", table->fields[i].name);
				    has_set = 1;
				    break;
			    default:
				    fprintf(f_sql, "`%s`", table->fields[i].name);
		  }
		  comma = 1;
	    }
	    fprintf(f_sql, ")");
	    comma = 0;
	    if (has_set) {
		    fprintf(f_sql, "\nSET\n");
		    for(i = 0; i < table->fields_count; i++) {
			    if(table->fields[i].type == FT_INTERNAL) continue;
			    switch(table->fields[i].type){
				    case FT_BLOB:
				    case FT_BIN:
					    if(comma) {
                            fprintf(f_sql, ",\n");
                        }
					    fprintf(f_sql, "    %s = UNHEX(@var_%s)", table->fields[i].name, table->fields[i].name);
					    comma = 1;
					    break;
				    case FT_BIT:
					    if(comma) {
                            fprintf(f_sql, ",\n");
                        }
					    fprintf(f_sql, "    %s = CAST(@var_%s AS UNSIGNED)", table->fields[i].name, table->fields[i].name);
					    comma = 1;
					    break;
				    default: break;
			    }
		    }
	    }

        fprintf(f_sql, ";\n");
	    fprintf(f_sql, "-- STATUS {\"records_expected\": %lu, \"records_dumped\": %lu, \"records_lost\": %s} STATUS END\n",
	    records_expected_total, records_dumped_total, records_lost ? "true": "false");
        if (delete == 1 && rm_dir(page_dir) == 0) {
          printf("delete the page files in %s\n", page_dir);
        }
    }

  for (i = 0; i < FILES_IN_DIR; i++) {
      free(ibfiles[i]);
      free(tablenames[i]);
      free(primary_page[i]);
      //memset(ibfiles[i], '\0', 256);
      //memset(tablenames[i], '\0', 256);
      //memset(primary_page[i], '\0', 350);
  }
  
  return 0;
}
