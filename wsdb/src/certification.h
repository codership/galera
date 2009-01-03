// Copyright (C) 2007 Codership Oy <info@codership.com>

#ifndef WSDB_CERTIFICATION_INCLUDED
#define WSDB_CERTIFICATION_INCLUDED

/*!
 * @brief initializes certification module
 * 
 * This method can be called during system initialization time
 * exactly once.
 * 
 * @param work_dir  directory, where write set databases are stored
 * @param base_name base name for write set db file
 * 
 * @return success code
 * @retval WSDB_OK
 */
int wsdb_cert_init(const char *work_dir, const char* base_name);

/*!
 * @brief shuts down certification module
 * 
 * This method erases certification indexes and frees memory
 * 
 * @return success code
 * @retval WSDB_OK
 */
int wsdb_cert_close();

#endif // WSDB_CERTIFICATION_INCLUDED
