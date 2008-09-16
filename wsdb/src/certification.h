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

/*!
 * @brief makes certification check for a write set
 * 
 * @param ws  write set to be certified
 * @param trx_seqno sequence number for the trx
 * 
 * @return success code, certification fail code or error code
 * @retval WSDB_OK
 * @retval WSDB_CERTIFICATION_FAIL certification failed, trx must abort
 */
int wsdb_certification_test(
    struct wsdb_write_set *ws, trx_seqno_t trx_seqno
);
#endif
