
/*. ******* coding:utf-8 AUTOHEADER START v1.1 *******
 *. vim: fileencoding=utf-8 syntax=c sw=8 ts=8 et
 *.
 *. © 2007-2009 Nima Talebi <nima@autonomy.net.au>
 *. © 2009      David Sommerseth <davids@redhat.com>
 *. © 2002-2008 Jean Delvare <khali@linux-fr.org>
 *. © 2000-2002 Alan Cox <alan@redhat.com>
 *.
 *. This file is part of Python DMI-Decode.
 *.
 *.     Python DMI-Decode is free software: you can redistribute it and/or modify
 *.     it under the terms of the GNU General Public License as published by
 *.     the Free Software Foundation, either version 2 of the License, or
 *.     (at your option) any later version.
 *.
 *.     Python DMI-Decode is distributed in the hope that it will be useful,
 *.     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *.     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *.     GNU General Public License for more details.
 *.
 *.     You should have received a copy of the GNU General Public License
 *.     along with Python DMI-Decode.  If not, see <http://www.gnu.org/licenses/>.
 *.
 *. THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *. WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *. MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
 *. EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *. INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *. LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *. PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *. LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 *. OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 *. ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *.
 *. ADAPTED M. STONE & T. PARKER DISCLAIMER: THIS SOFTWARE COULD RESULT IN INJURY
 *. AND/OR DEATH, AND AS SUCH, IT SHOULD NOT BE BUILT, INSTALLED OR USED BY ANYONE.
 *.
 *. $AutoHeaderSerial::20090522                                                 $
 *. ******* AUTOHEADER END v1.1 ******* */

#include <Python.h>

#include <libxml/tree.h>
#include "libxml_wrap.h"

#include "dmidecodemodule.h"
#include "dmixml.h"
#include "dmierror.h"
#include "dmilog.h"
#include "xmlpythonizer.h"
#include "version.h"
#include "dmidump.h"
#include <mcheck.h>

#if (PY_VERSION_HEX < 0x03030000)
char *PyUnicode_AsUTF8(PyObject *unicode) {
        PyObject *as_bytes = PyUnicode_AsUTF8String(unicode);
        if (!as_bytes) {
                return NULL;
        }

        return PyBytes_AsString(as_bytes);
}
#endif

static void init(options *opt)
{
	int efi;
	size_t fp;

        opt->dumpfile = NULL;
        opt->flags = 0;
        opt->type = -1;
        opt->dmiversion_n = NULL;
        opt->mappingxml = NULL;
        opt->python_xml_map = strdup(PYTHON_XML_MAP);
        opt->logdata = log_init();

        efi = address_from_efi(opt->logdata, &fp);
        if(efi == EFI_NOT_FOUND){
                opt->devmem = DEFAULT_MEM_DEV;
        } else {
                opt->devmem = SYS_TABLE_FILE;
        }

        /* sanity check */
        if(sizeof(u8) != 1 || sizeof(u16) != 2 || sizeof(u32) != 4 || '\0' != 0) {
                log_append(opt->logdata, LOGFL_NORMAL, LOG_WARNING,
                           "%s: compiler incompatibility", "dmidecodemodule");
        }
}

int parse_opt_type(Log_t *logp, const char *arg)
{
        while(*arg != '\0') {
                int val;
                char *next;

                val = strtoul(arg, &next, 0);
                if(next == arg) {
                        log_append(logp, LOGFL_NODUPS, LOG_ERR, "Invalid type keyword: %s", arg);
                        return -1;
                }
                if(val > 0xff) {
                        log_append(logp, LOGFL_NODUPS, LOG_ERR, "Invalid type number: %i", val);
                        return -1;
                }

                if( val >= 0 ) {
                        return val;
                }
                arg = next;
                while(*arg == ',' || *arg == ' ')
                        arg++;
        }
        return -1;
}


xmlNode *dmidecode_get_version(options *opt)
{
        int found = 0;
        size_t fp;
        int efi;
        u8 *buf = NULL;
        xmlNode *ver_n = NULL;
	size_t size;

	/* 
	 * First, if devmem is available, set default as DEFAULT_MEM_DEV
         * Set default option values 
	 */
        if( opt->devmem == NULL ) {
		efi = address_from_efi(opt->logdata, &fp);
		if(efi == EFI_NOT_FOUND){
			opt->devmem = DEFAULT_MEM_DEV;
		} else {
                	opt->devmem = SYS_TABLE_FILE;
		}
        }

        /* Read from dump if so instructed */
        if(opt->dumpfile != NULL) {
                //. printf("Reading SMBIOS/DMI data from file %s.\n", dumpfile);
                if((buf = mem_chunk(opt->logdata, 0, 0x20, opt->dumpfile)) != NULL) {
                        if (memcmp(buf, "_SM3_", 5) == 0) {
                                ver_n = smbios3_decode_get_version(buf, opt->dumpfile);
                                if (dmixml_GetAttrValue(ver_n, "unknown") == NULL)
                                        found++;
                        } else if (memcmp(buf, "_SM_", 4) == 0) {
                                ver_n = smbios_decode_get_version(buf, opt->dumpfile);
                                if (dmixml_GetAttrValue(ver_n, "unknown") == NULL)
                                        found++;
                        } else if (memcmp(buf, "_DMI_", 5) == 0) {
                                ver_n = legacy_decode_get_version(buf, opt->dumpfile);
                                if (dmixml_GetAttrValue(ver_n, "unknown") == NULL)
                                        found++;
                        }
                } else {
                        ver_n = NULL;
                        goto exit_free;
                }

        }

        /*
         * First try reading from sysfs tables.  The entry point file could
         * contain one of several types of entry points, so read enough for
         * the largest one, then determine what type it contains.
         */
        size = 0x20;
	if ( (buf = read_file(opt->logdata, 0, &size, SYS_ENTRY_FILE)) != NULL ){
		if(size >= 24 && memcmp(buf, "_SM3_", 5) == 0){
			ver_n = smbios3_decode_get_version(buf, opt->devmem);
			if (dmixml_GetAttrValue(ver_n, "unknown") == NULL)
				found++;
		} else if (size >= 31 && memcmp(buf, "_SM_", 4) == 0 ){
			ver_n = smbios_decode_get_version(buf, opt->devmem);
			if (dmixml_GetAttrValue(ver_n, "unknown") == NULL)
				found++;
		} else if (size >= 15 && memcmp(buf, "_DMI_", 5) == 0){
			ver_n = legacy_decode_get_version (buf, opt->devmem);
			if (dmixml_GetAttrValue(ver_n, "unknown") == NULL)
				found++;
		}

		if(found)
			goto done;
	} else {
		ver_n = NULL;
		goto exit_free;
	}

        /* Read from /dev/mem */
        /* Next try EFI (ia64, Intel-based Mac) */
	efi = address_from_efi(opt->logdata, &fp);
	switch(efi){
		case EFI_NOT_FOUND:
			goto memory_scan;
		case EFI_NO_SMBIOS:
			ver_n = NULL;
			goto exit_free;
	}

	if((buf = mem_chunk(opt->logdata, fp, 0x20, opt->devmem)) == NULL){
		ver_n = NULL;
		goto exit_free;
	}

	if(memcmp(buf, "_SM3_", 5) == 0){
                ver_n = smbios3_decode_get_version(buf, opt->devmem);
                if(dmixml_GetAttrValue(ver_n, "unknown") == NULL)
                        found++;
	} else if (memcmp(buf, "_SM_", 4) == 0 ) {
                ver_n = smbios_decode_get_version(buf, opt->devmem);
                if(dmixml_GetAttrValue(ver_n, "unknown") == NULL)
                        found++;
	}

	goto done;

memory_scan:
#if defined __i386__ || defined __x86_64__
	/* Fallback to memory scan (x86, x86_64) */
	if((buf = mem_chunk(opt->logdata, 0xF0000, 0x10000, opt->devmem)) == NULL) {
		ver_n = NULL;
		goto exit_free;
	}

	/* Look for a 64-bit entry point first */
	for (fp = 0; fp <= 0xFFE0; fp+= 16){
		if(memcmp(buf+fp, "_SM3_", 5) == 0){
			ver_n = smbios3_decode_get_version(buf+fp, opt->devmem);
			if( dmixml_GetAttrValue(ver_n, "unknown") == NULL ) {
				found++;
				goto done;
			}
		}
	}

	/* If none found, look for a 32-bit entry point */
	for(fp = 0; fp <= 0xFFF0; fp += 16) {
		if(memcmp(buf + fp, "_SM_", 4) == 0 && fp <= 0xFFE0) {
			ver_n = smbios_decode_get_version(buf + fp, opt->devmem);
			if ( dmixml_GetAttrValue(ver_n, "unknown") == NULL ) {
				found++;
				goto done;
			}
			fp += 16;
		} else if (memcmp(buf + fp, "_DMI_", 5) == 0) {
			ver_n = legacy_decode_get_version (buf + fp, opt->devmem);
			if( dmixml_GetAttrValue(ver_n, "unknown") == NULL ) {
				found++;
				goto done;
			}
		}
	}
#endif

done:
	if(!found){
                log_append(opt->logdata, LOGFL_NODUPS, LOG_WARNING,
                        "No SMBIOS nor DMI entry point found, sorry.");
	}

exit_free:
	if (buf != NULL)
		free(buf);

	return ver_n;

}

int dmidecode_get_xml(options *opt, xmlNode* dmixml_n)
{
        assert(dmixml_n != NULL);
        if(dmixml_n == NULL) {
                return 0;
        }
        //mtrace();

        int ret = 0;
        int found = 0;
        size_t fp;
        int efi;
        u8 *buf = NULL;
	size_t size;

        const char *f = opt->dumpfile ? opt->dumpfile : opt->devmem;
        if(access(f, R_OK) < 0) {
                log_append(opt->logdata, LOGFL_NORMAL,
                           LOG_WARNING, "Permission denied to memory file/device (%s)", f);
                return 0;
        }

        /* Read from dump if so instructed */
        if(opt->dumpfile != NULL) {
		if((buf = mem_chunk(opt->logdata, 0, 0x20, opt->dumpfile)) == NULL) {
			ret = 1;
			goto exit_free;
		}
		if(memcmp(buf, "_SM3_", 5) == 0){
			if(smbios3_decode(opt->logdata, opt->type, buf,opt->dumpfile, 0, dmixml_n))
				found++;
		} else if (memcmp(buf, "_SM_", 4) == 0){
			if(smbios_decode(opt->logdata, opt->type, buf, opt->dumpfile, 0, dmixml_n))
				found++;
		} else if (memcmp(buf, "_DMI_", 5) == 0){
			if(legacy_decode(opt->logdata, opt->type, buf, opt->dumpfile, 0, dmixml_n))
				found++;
		}
		goto done;
	}

        /*
         * First try reading from sysfs tables.  The entry point file could
         * contain one of several types of entry points, so read enough for
         * the largest one, then determine what type it contains.
         */
	size = 0x20;
	if ( (buf = read_file(opt->logdata, 0, &size, SYS_ENTRY_FILE)) != NULL){
		if ( size >= 24 &&  memcmp(buf, "_SM3_", 5) == 0) {
			if (smbios3_decode(opt->logdata, opt->type, buf, SYS_TABLE_FILE, FLAG_NO_FILE_OFFSET, dmixml_n))
				found++;
		} else if (size >= 31 && memcmp(buf, "_SM_", 4) == 0){
			if (smbios_decode(opt->logdata, opt->type, buf, SYS_TABLE_FILE, FLAG_NO_FILE_OFFSET, dmixml_n))
				found++;
		} else if (size >= 15 && memcmp(buf, "_DMI_", 5) == 0){
			if (legacy_decode(opt->logdata, opt->type, buf, SYS_TABLE_FILE, FLAG_NO_FILE_OFFSET, dmixml_n))
				found++;
		}
		if (found)
			goto done;
	} else {
		ret = 1;
		goto done;
	}

	/* Next try EFI (ia64, Intel-based Mac) */
	efi = address_from_efi(opt->logdata, &fp);
	switch(efi){
		case EFI_NOT_FOUND:
			goto memory_scan;
		case EFI_NO_SMBIOS:
			ret = 1;
			goto exit_free;
	}

	if((buf = mem_chunk(opt->logdata, fp, 0x20, opt->devmem)) == NULL ){
		ret = 1;
		goto exit_free;
	}

	if (memcmp(buf, "_SM3_", 5) == 0){
		if (smbios3_decode(opt->logdata, opt->type, buf + fp, opt->devmem, 0, dmixml_n))
			found++;
	} else if (memcmp(buf, "_SM_", 4) == 0){
		if(smbios_decode(opt->logdata, opt->type, buf + fp, opt->devmem, 0, dmixml_n))
			found++;
	}

	goto done;

memory_scan:
#if defined __i386__ || defined __x86_64__
        if((buf = mem_chunk(opt->logdata, 0xF0000, 0x10000, opt->devmem)) == NULL)
        {
                ret = 1;
                goto exit_free;
        }

        /* Look for a 64-bit entry point first */
        for (fp = 0; fp <= 0xFFE0; fp += 16){
                if (memcmp(buf + fp, "_SM3_", 5) == 0)
                {
                        if(smbios3_decode(opt->logdata, opt->type,
                                buf + fp, opt->devmem, 0, dmixml_n)){
                                found++;
                                goto done;
                        }
                }
        }

        /* If none found, look for a 32-bit entry point */
        for(fp = 0; fp <= 0xFFF0; fp += 16) {
                if(memcmp(buf + fp, "_SM_", 4) == 0 && fp <= 0xFFE0) {
                        if(smbios_decode(opt->logdata, opt->type,
                                buf + fp, opt->devmem, 0, dmixml_n)) {
                                found++;
                                goto done;
                        }
                } else if(memcmp(buf + fp, "_DMI_", 5) == 0) {
                        if(legacy_decode(opt->logdata, opt->type,
                                buf + fp, opt->devmem, 0, dmixml_n)) {
                                found++;
                                goto done;
                                }
                        }
                }
#endif

done:
        if( !found ) {
                log_append(opt->logdata, LOGFL_NODUPS, LOG_WARNING,
                        "No SMBIOS nor DMI entry point found, sorry.");
        }

exit_free:
        if(buf != NULL)
                free(buf);

        return ret;
}

xmlNode* load_mappingxml(options *opt) {
       if( opt->mappingxml == NULL ) {
                // Load mapping into memory
                opt->mappingxml = xmlReadFile(opt->python_xml_map, NULL, 0);
                if( opt->mappingxml == NULL ) {
                        PyReturnError(PyExc_IOError, "Could not open tje XML mapping file '%s'",
                                      opt->python_xml_map);
                }
       }
       return dmiMAP_GetRootElement(opt->mappingxml);
}

xmlNode *__dmidecode_xml_getsection(options *opt, const char *section) {
        xmlNode *dmixml_n = NULL;
        xmlNode *group_n = NULL;

        dmixml_n = xmlNewNode(NULL, (xmlChar *) "dmidecode");
        assert( dmixml_n != NULL );
        // Append DMI version info
        if( opt->dmiversion_n != NULL ) {
                xmlAddChild(dmixml_n, xmlCopyNode(opt->dmiversion_n, 1));
        }

        // Fetch the Mapping XML file
        if( (group_n = load_mappingxml(opt)) == NULL) {
                xmlFreeNode(dmixml_n);
                // Exception already set by calling function
                return NULL;
        }

        // Find the section in the XML containing the group mappings
        if( (group_n = dmixml_FindNode(group_n, "GroupMapping")) == NULL ) {
                PyReturnError(PyExc_LookupError,
                              "Could not find the GroupMapping section in the XML mapping");
        }

        // Find the XML node containing the Mapping section requested to be decoded
        if( (group_n = dmixml_FindNodeByAttr(group_n, "Mapping", "name", section)) == NULL ) {
                PyReturnError(PyExc_LookupError,
                              "Could not find the XML->Python Mapping section for '%s'", section);
        }

        if( group_n->children == NULL ) {
                PyReturnError(PyExc_RuntimeError,
                              "Mapping is empty for the '%s' section in the XML mapping", section);
        }

        // Go through all TypeMap's belonging to this Mapping section
        foreach_xmlnode(dmixml_FindNode(group_n, "TypeMap"), group_n) {
                char *typeid = dmixml_GetAttrValue(group_n, "id");

                if( group_n->type != XML_ELEMENT_NODE ) {
                        continue;
                }

                // The children of <Mapping> tags must only be <TypeMap> and
                // they must have an 'id' attribute
                if( (typeid == NULL) || (xmlStrcmp(group_n->name, (xmlChar *) "TypeMap") != 0) ) {
                        PyReturnError(PyExc_RuntimeError, "Invalid TypeMap node in mapping XML");
                }

                // Parse the typeid string to a an integer
                opt->type = parse_opt_type(opt->logdata, typeid);
                if(opt->type == -1) {
                        char *err = log_retrieve(opt->logdata, LOG_ERR);
                        log_clear_partial(opt->logdata, LOG_ERR, 0);
                        int typeid_int = atoi(typeid); 
                        _pyReturnError(PyExc_RuntimeError, "Invalid type id '%d' -- %s", typeid_int, err);
                        free(err);
                        return NULL;
                }

                // Parse the DMI data and put the result into dmixml_n node chain.
                if( dmidecode_get_xml(opt, dmixml_n) != 0 ) {
                        PyReturnError(PyExc_RuntimeError, "Error decoding DMI data");
                }
        }
#if 0  // DEBUG - will dump generated XML to stdout
        xmlDoc *doc = xmlNewDoc((xmlChar *) "1.0");
        xmlDocSetRootElement(doc, xmlCopyNode(dmixml_n, 1));
        xmlSaveFormatFileEnc("-", doc, "UTF-8", 1);
        xmlFreeDoc(doc);
#endif
        return dmixml_n;
}

static PyObject *dmidecode_get_group(options *opt, const char *section)
{
	int efi;
	size_t fp;
        PyObject *pydata = NULL;
        xmlNode *dmixml_n = NULL;
        ptzMAP *mapping = NULL;

        /* Set default option values */
        if( opt->devmem == NULL ) {
                efi = address_from_efi(opt->logdata, &fp);
                if(efi == EFI_NOT_FOUND){
                        opt->devmem = DEFAULT_MEM_DEV;
                } else {
                        opt->devmem = SYS_TABLE_FILE;
                }
	}
        opt->flags = 0;

        // Decode the dmidata into an XML node
        dmixml_n = __dmidecode_xml_getsection(opt, section);
        if( dmixml_n == NULL ) {
                // Exception already set
                return NULL;
        }

        // Convert the retrieved XML nodes to a Python dictionary
        mapping = dmiMAP_ParseMappingXML_GroupName(opt->logdata, opt->mappingxml, section);
        if( mapping == NULL ) {
                // Exception already set
                xmlFreeNode(dmixml_n);
                return NULL;
        }

        // Generate Python dict out of XML node
        pydata = pythonizeXMLnode(opt->logdata, mapping, dmixml_n);

        // Clean up and return the resulting Python dictionary
        ptzmap_Free(mapping);
        xmlFreeNode(dmixml_n);

        return pydata;
}


xmlNode *__dmidecode_xml_gettypeid(options *opt, int typeid)
{
	int efi;
        size_t fp;
        xmlNode *dmixml_n = NULL;

        /* Set default option values */
        if( opt->devmem == NULL ) {
                efi = address_from_efi(opt->logdata, &fp);
                if(efi == EFI_NOT_FOUND){
                        opt->devmem = DEFAULT_MEM_DEV;
                } else {
                        opt->devmem = SYS_TABLE_FILE;
                }
        }
        opt->flags = 0;

        dmixml_n = xmlNewNode(NULL, (xmlChar *) "dmidecode");
        assert( dmixml_n != NULL );
        // Append DMI version info
        if( opt->dmiversion_n != NULL ) {
                xmlAddChild(dmixml_n, xmlCopyNode(opt->dmiversion_n, 1));
        }

        // Fetch the Mapping XML file
        if( load_mappingxml(opt) == NULL) {
                xmlFreeNode(dmixml_n);
                return NULL;
        }

        // Parse the DMI data and put the result into dmixml_n node chain.
        opt->type = typeid;
        if( dmidecode_get_xml(opt, dmixml_n) != 0 ) {
                PyReturnError(PyExc_RuntimeError, "Error decoding DMI data");
        }

        return dmixml_n;
}


static PyObject *dmidecode_get_typeid(options *opt, int typeid)
{
        PyObject *pydata = NULL;
        xmlNode *dmixml_n = NULL;
        ptzMAP *mapping = NULL;

        dmixml_n = __dmidecode_xml_gettypeid(opt, typeid);
        if( dmixml_n == NULL ) {
                // Exception already set
                return NULL;
        }

        // Convert the retrieved XML nodes to a Python dictionary
        mapping = dmiMAP_ParseMappingXML_TypeID(opt->logdata, opt->mappingxml, opt->type);
        if( mapping == NULL ) {
                // FIXME:  Should we raise an exception here?
                // Now it passes the unit-test
                return PyDict_New();
        }

        // Generate Python dict out of XML node
        pydata = pythonizeXMLnode(opt->logdata, mapping, dmixml_n);

        // Clean up and return the resulting Python dictionary
        ptzmap_Free(mapping);
        xmlFreeNode(dmixml_n);

        return pydata;
}


// This global variable should only be available for the "first-entry" functions
// which is defined in PyMethodDef DMIDataMethods[].
options *global_options = NULL;

static PyObject *dmidecode_get_bios(PyObject * self, PyObject * args)
{
        return dmidecode_get_group(global_options, "bios");
}
static PyObject *dmidecode_get_system(PyObject * self, PyObject * args)
{
        return dmidecode_get_group(global_options, "system");
}
static PyObject *dmidecode_get_baseboard(PyObject * self, PyObject * args)
{
        return dmidecode_get_group(global_options, "baseboard");
}
static PyObject *dmidecode_get_chassis(PyObject * self, PyObject * args)
{
        return dmidecode_get_group(global_options, "chassis");
}
static PyObject *dmidecode_get_processor(PyObject * self, PyObject * args)
{
        return dmidecode_get_group(global_options, "processor");
}
static PyObject *dmidecode_get_memory(PyObject * self, PyObject * args)
{
        return dmidecode_get_group(global_options, "memory");
}
static PyObject *dmidecode_get_cache(PyObject * self, PyObject * args)
{
        return dmidecode_get_group(global_options, "cache");
}
static PyObject *dmidecode_get_connector(PyObject * self, PyObject * args)
{
        return dmidecode_get_group(global_options, "connector");
}
static PyObject *dmidecode_get_slot(PyObject * self, PyObject * args)
{
        return dmidecode_get_group(global_options, "slot");
}

static PyObject *dmidecode_get_section(PyObject *self, PyObject *args)
{
        char *section = NULL;
        if (PyUnicode_Check(args)) {
                section = PyUnicode_AsUTF8(args);
        } else if (PyBytes_Check(args)) {
                section = PyBytes_AsString(args);
        }

        if( section != NULL ) {
                return dmidecode_get_group(global_options, section);
        }
        PyReturnError(PyExc_RuntimeError, "No section name was given");
}

static PyObject *dmidecode_get_type(PyObject * self, PyObject * args)
{
        int typeid;
        PyObject *pydata = NULL;

        if( PyArg_ParseTuple(args, (char *)"i", &typeid) ) {
                if( (typeid < 0) || (typeid > 255) ) {
                        Py_RETURN_FALSE;
                        // FIXME:  Should send exception instead
                        // PyReturnError(PyExc_RuntimeError, "Types are bound between 0 and 255 (inclusive)."
                        //               "Type value used was '%i'", typeid);
                }
        } else {
                PyReturnError(PyExc_RuntimeError, "Type '%i' is not a valid type identifier%c", typeid);
        }

        pydata = dmidecode_get_typeid(global_options, typeid);
        return pydata;
}

static PyObject *dmidecode_xmlapi(PyObject *self, PyObject *args, PyObject *keywds)
{
        static char *keywordlist[] = {"query_type", "result_type", "section", "typeid", NULL};
        PyObject *pydata = NULL;
        xmlDoc *dmixml_doc = NULL;
        xmlNode *dmixml_n = NULL;
        char *sect_query = NULL, *qtype = NULL, *rtype = NULL;
        int type_query = -1;

        // Parse the keywords - we only support keywords, as this is an internal API
        if( !PyArg_ParseTupleAndKeywords(args, keywds, "ss|si", keywordlist,
                                         &qtype, &rtype, &sect_query, &type_query) ) {
                return NULL;
        }

        // Check for sensible arguments and retrieve the xmlNode with DMI data
        switch( *qtype ) {
        case 's': // Section / GroupName
                if( sect_query == NULL ) {
                        PyReturnError(PyExc_TypeError, "section keyword cannot be NULL")
                }
                dmixml_n = __dmidecode_xml_getsection(global_options, sect_query);
                break;

        case 't': // TypeID / direct TypeMap
                if( type_query < 0 ) {
                        PyReturnError(PyExc_TypeError,
                                      "typeid keyword must be set and must be a positive integer");
                } else if( type_query > 255 ) {
                        PyReturnError(PyExc_ValueError,
                                      "typeid keyword must be an integer between 0 and 255");
                }
                dmixml_n = __dmidecode_xml_gettypeid(global_options, type_query);
                break;

        default:
                PyReturnError(PyExc_TypeError, "Internal error - invalid query type '%c'", *qtype);
        }

        // Check if we got any data
        if( dmixml_n == NULL ) {
                // Exception already set
                return NULL;
        }

        // Check for sensible return type and wrap the correct type into a Python Object
        switch( *rtype ) {
        case 'n':
                pydata = libxml_xmlNodePtrWrap((xmlNode *) dmixml_n);
                break;

        case 'd':
                dmixml_doc = xmlNewDoc((xmlChar *) "1.0");
                if( dmixml_doc == NULL ) {
                        PyReturnError(PyExc_MemoryError, "Could not create new XML document");
                }
                xmlDocSetRootElement(dmixml_doc, dmixml_n);
                pydata = libxml_xmlDocPtrWrap((xmlDoc *) dmixml_doc);
                break;

        default:
                PyReturnError(PyExc_TypeError, "Internal error - invalid result type '%c'", *rtype);
        }

        // Return XML data
        Py_INCREF(pydata);
        return pydata;
}



static PyObject *dmidecode_dump(PyObject * self, PyObject * null)
{
        const char *f;
        struct stat _buf;

        f = (global_options->dumpfile ? global_options->dumpfile : global_options->devmem);
        stat(f, &_buf);

        if( (access(f, F_OK) != 0) || ((access(f, W_OK) == 0) && S_ISREG(_buf.st_mode)) ) {
                if( dump(SYS_TABLE_FILE, f) ) {
                        Py_RETURN_TRUE;
                }
        }
        Py_RETURN_FALSE;
}

static PyObject *dmidecode_get_dev(PyObject * self, PyObject * null)
{
        PyObject *dev = NULL;
        dev = PYTEXT_FROMSTRING((global_options->dumpfile != NULL
                                   ? global_options->dumpfile : global_options->devmem));
        Py_INCREF(dev);
        return dev;
}

static PyObject *dmidecode_set_dev(PyObject * self, PyObject * arg)
{
        char *f = NULL;
        if(PyUnicode_Check(arg)) {
                f = PyUnicode_AsUTF8(arg);
        } else if(PyBytes_Check(arg)) {
                f = PyBytes_AsString(arg);
        }
        if(f) {
                struct stat buf;

                if( (f != NULL) && (global_options->dumpfile != NULL )
                    && (strcmp(global_options->dumpfile, f) == 0) ) {
                        Py_RETURN_TRUE;
                }
                if( (f == NULL) || (strlen(f) < 0) ) {
                        PyReturnError(PyExc_RuntimeError, "set_dev() file name string cannot be empty");
                }

                errno = 0;
                if( stat(f, &buf) < 0 ) {
                        if( errno == ENOENT ) {
                                // If this file does not exist, that's okay.
                                // python-dmidecode will create it.
                                global_options->dumpfile = strdup(f);
                                Py_RETURN_TRUE;
                        }
                        PyReturnError(PyExc_RuntimeError, strerror(errno));
                }
                if(S_ISCHR(buf.st_mode)) {
                        if(memcmp(f, "/dev/mem", 8) == 0) {
                                if( global_options->dumpfile != NULL ) {
                                        free(global_options->dumpfile);
                                        global_options->dumpfile = NULL;
                                }
                                Py_RETURN_TRUE;
                        } else {
                                PyReturnError(PyExc_RuntimeError, "Invalid memory device: %s", f);
                        }
                } else if(S_ISREG(buf.st_mode) || S_ISLNK(buf.st_mode) ) {
                        global_options->dumpfile = strdup(f);
                        Py_RETURN_TRUE;
                }
        }
        PyReturnError(PyExc_RuntimeError, "set_dev(): Invalid input");
}

static PyObject *dmidecode_set_pythonxmlmap(PyObject * self, PyObject * arg)
{
        char *fname = NULL;

        if (PyUnicode_Check(arg)) {
                fname = PyUnicode_AsUTF8(arg);
        } else if (PyBytes_Check(arg)) {
                fname = PyBytes_AsString(arg);
        }
        if (fname) {
                struct stat fileinfo;

                memset(&fileinfo, 0, sizeof(struct stat));
                if( stat(fname, &fileinfo) != 0 ) {
                        PyReturnError(PyExc_IOError, "Could not access the file '%s'", fname);
                }

                free(global_options->python_xml_map);
                global_options->python_xml_map = strdup(fname);
                Py_RETURN_TRUE;
        } else {
                Py_RETURN_FALSE;
        }
}


static PyObject * dmidecode_get_warnings(PyObject *self, PyObject *null)
{
        char *warn = NULL;
        PyObject *ret = NULL;

        warn = log_retrieve(global_options->logdata, LOG_WARNING);
        if( warn ) {
                ret = PYTEXT_FROMSTRING(warn);
                free(warn);
        } else {
                ret = Py_None;
        }
        return ret;
}


static PyObject * dmidecode_clear_warnings(PyObject *self, PyObject *null)
{
        log_clear_partial(global_options->logdata, LOG_WARNING, 1);
        Py_RETURN_TRUE;
}


static PyMethodDef DMIDataMethods[] = {
        {(char *)"dump", dmidecode_dump, METH_NOARGS, (char *)"Dump dmidata to set file"},
        {(char *)"get_dev", dmidecode_get_dev, METH_NOARGS,
         (char *)"Get an alternative memory device file"},
        {(char *)"set_dev", dmidecode_set_dev, METH_O,
         (char *)"Set an alternative memory device file"},

        {(char *)"bios", dmidecode_get_bios, METH_VARARGS, (char *)"BIOS Data"},
        {(char *)"system", dmidecode_get_system, METH_VARARGS, (char *)"System Data"},
        {(char *)"baseboard", dmidecode_get_baseboard, METH_VARARGS, (char *)"Baseboard Data"},
        {(char *)"chassis", dmidecode_get_chassis, METH_VARARGS, (char *)"Chassis Data"},
        {(char *)"processor", dmidecode_get_processor, METH_VARARGS, (char *)"Processor Data"},
        {(char *)"memory", dmidecode_get_memory, METH_VARARGS, (char *)"Memory Data"},
        {(char *)"cache", dmidecode_get_cache, METH_VARARGS, (char *)"Cache Data"},
        {(char *)"connector", dmidecode_get_connector, METH_VARARGS, (char *)"Connector Data"},
        {(char *)"slot", dmidecode_get_slot, METH_VARARGS, (char *)"Slot Data"},

        {(char *)"QuerySection", dmidecode_get_section, METH_O,
         (char *) "Queries the DMI data structure for a given section name.  A section"
         "can often contain several DMI type elements"
        },

        {(char *)"type", dmidecode_get_type, METH_VARARGS, (char *)"By Type"},

        {(char *)"QueryTypeId", dmidecode_get_type, METH_VARARGS,
         (char *) "Queries the DMI data structure for a specific DMI type."
        },

        {(char *)"pythonmap", dmidecode_set_pythonxmlmap, METH_O,
         (char *) "Use another python dict map definition. The default file is " PYTHON_XML_MAP},

        {"xmlapi", (PyCFunction)dmidecode_xmlapi, METH_VARARGS | METH_KEYWORDS, "Internal API for retrieving data as raw XML data"},


        {(char *)"get_warnings", dmidecode_get_warnings, METH_NOARGS,
         (char *) "Retrieve warnings from operations"},

        {(char *)"clear_warnings", dmidecode_clear_warnings, METH_NOARGS,
         (char *) "Clear all warnings"},

        {NULL, NULL, 0, NULL}
};

void destruct_options(void *ptr)
{
#ifdef IS_PY3K
        ptr = PyCapsule_GetPointer(ptr, NULL);
#endif
        options *opt = (options *) ptr;

        if( opt->mappingxml != NULL ) {
                xmlFreeDoc(opt->mappingxml);
                opt->mappingxml = NULL;
        }

        if( opt->python_xml_map != NULL ) {
                free(opt->python_xml_map);
                opt->python_xml_map = NULL;
        }

        if( opt->dmiversion_n != NULL ) {
                xmlFreeNode(opt->dmiversion_n);
                opt->dmiversion_n = NULL;
        }

        if( opt->dumpfile != NULL ) {
                free(opt->dumpfile);
                opt->dumpfile = NULL;
        }

        if( opt->logdata != NULL ) {
                char *warn = NULL;

                log_clear_partial(opt->logdata, LOG_WARNING, 0);
                warn = log_retrieve(opt->logdata, LOG_WARNING);
                if( warn ) {
                        fprintf(stderr, "\n** COLLECTED WARNINGS **\n%s** END OF WARNINGS **\n\n", warn);
                        free(warn);
                }
                log_close(opt->logdata);
        }

        free(ptr);
}

#ifdef IS_PY3K
static struct PyModuleDef dmidecodemod_def = {
    PyModuleDef_HEAD_INIT,
    "dmidecodemod",
    NULL,
    -1,
    DMIDataMethods,
    NULL,
    NULL,
    NULL,
    NULL
};

PyMODINIT_FUNC
PyInit_dmidecodemod(void)
#else
PyMODINIT_FUNC
initdmidecodemod(void)
#endif
{
        char *dmiver = NULL;
        PyObject *module = NULL;
        PyObject *version = NULL;
        options *opt;

        xmlInitParser();
        xmlXPathInit();

        opt = (options *) malloc(sizeof(options)+2);
        if (opt == NULL)
                MODINITERROR;

        memset(opt, 0, sizeof(options)+2);
        init(opt);
#ifdef IS_PY3K
        module = PyModule_Create(&dmidecodemod_def);
#else
        module = Py_InitModule3((char *)"dmidecodemod", DMIDataMethods,
                                "Python extension module for dmidecode");
#endif
        if (module == NULL) {
                free(opt);
                MODINITERROR;
        }

        version = PYTEXT_FROMSTRING(VERSION);
        Py_INCREF(version);
        PyModule_AddObject(module, "version", version);

        opt->dmiversion_n = dmidecode_get_version(opt);
        dmiver = dmixml_GetContent(opt->dmiversion_n);
        PyModule_AddObject(module, "dmi", dmiver ? PYTEXT_FROMSTRING(dmiver) : Py_None);

        // Assign this options struct to the module as well with a destructor, that way it will
        // clean up the memory for us.
        // TODO: destructor has wrong type under py3?
        PyModule_AddObject(module, "options", PyCapsule_New(opt, NULL, destruct_options));
        global_options = opt;
#ifdef IS_PY3K
        return module;
#endif
}
