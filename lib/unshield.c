/* $Id$ */
#define _BSD_SOURCE 1
#include "unshield_internal.h"
#include "cabfile.h"

#include <synce_log.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/**
  Create filename pattern used by unshield_fopen_for_reading()
 */
static bool unshield_create_filename_pattern(Unshield* unshield, const char* filename)/*{{{*/
{
  if (unshield && filename)
  {
    char pattern[256];
    char* prefix = strdup(filename);
    char* p = strrchr(prefix, '/');
    if (!p)
      p = prefix;

    for (; *p != '\0'; p++)
    {
      if ('.' == *p || isdigit(*p))
      {
        *p = '\0';
        break;
      }
    }

    snprintf(pattern, sizeof(pattern), "%s%%i.%%s", prefix);
    free(prefix);

    FREE(unshield->filename_pattern);
    unshield->filename_pattern = strdup(pattern);
    return true;
  }
  else
    return false;
}/*}}}*/

static bool unshield_create_header_shortcuts(Header* header)
{
  header->common      = (CommonHeader*)header->data;
  header->cab         = (CabDescriptor*)(header->data + letoh32(header->common->cab_descriptor_offset));
  header->file_table  = (uint32_t*)((uint8_t*)header->cab + letoh32(header->cab->file_table_offset));
  return true;
}

/**
  Read all header files
 */
static bool unshield_read_headers(Unshield* unshield)/*{{{*/
{
  int i;
  bool iterate = true;
  Header* previous = NULL;

  if (unshield->header_list)
  {
    synce_warning("Already have a header list");
    return true;
  }

  for (i = 1; iterate; i++)
  {
    FILE* file = unshield_fopen_for_reading(unshield, i, HEADER_SUFFIX);
    
    if (file)
      iterate = false;
    else
      file = unshield_fopen_for_reading(unshield, i, CABINET_SUFFIX);

    if (file)
    {
      size_t bytes_read;
      Header* header = NEW1(Header);
      header->index = i;

      header->size = FSIZE(file);
      if (header->size < 4)
      {
        synce_error("Header file %i too small", i);
        goto error;
      }

      header->data = malloc(header->size);
      if (!header->data)
      {
        synce_error("Failed to allocate memory for header file %i", i);
        goto error;
      }

      bytes_read = fread(header->data, 1, header->size, file);
      FCLOSE(file);

      if (bytes_read != header->size)
      {
        synce_error("Failed to read from header file %i. Expected = %i, read = %i", 
            i, header->size, bytes_read);
        goto error;
      }

      if (!unshield_create_header_shortcuts(header))
      {
        synce_error("Failed to create header shortcuts for header file %i", i);
        goto error;
      }
      
      if (CAB_SIGNATURE != letoh32(header->common->signature))
      {
        synce_error("Invalid file signature for header file %i", i);
        goto error;
      }

      synce_trace("Version: 0x%08x", letoh32(header->common->version));

      if (((letoh32(header->common->version) >> 12) & 0xf) == 6)
        unshield->major_version = 6;
      else
        unshield->major_version = 5;

      if (previous)
        previous->next = header;
      else
        previous = unshield->header_list = header;

      synce_trace("Got header file %i", i);

      continue;

error:
      if (header)
        FREE(header->data);
      FREE(header);
      iterate = false;
    }
    else
      iterate = false;
  }

  return (unshield->header_list != NULL);
}/*}}}*/

Unshield* unshield_open(const char* filename)/*{{{*/
{
  Unshield* unshield = NEW1(Unshield);
  if (!unshield)
  {
    synce_error("Failed to allocate memory for Unshield structure");
    goto error;
  }

  if (!unshield_create_filename_pattern(unshield, filename))
  {
    synce_error("Failed to create filename pattern");
    goto error;
  }

  if (!unshield_read_headers(unshield))
  {
    synce_error("Failed to read header files");
    goto error;
  }

  return unshield;

error:
  unshield_close(unshield);
  return NULL;
}/*}}}*/

void unshield_close(Unshield* unshield)/*{{{*/
{
  if (unshield)
  {
    Header* header;

    for(header = unshield->header_list; header; )
    {
      Header* next = header->next;

      FREE(header->data);
      FREE(header);

      header = next;
    }

    FREE(unshield->filename_pattern);
    free(unshield);
  }
}/*}}}*/

