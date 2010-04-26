//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

//!
// @file certification.hpp
//
// @brief Certification interface definition
//

#ifndef GALERA_CERTIFICATION_HPP
#define GALERA_CERTIFICATION_HPP

#include "trx_handle.hpp"

#include <string>

namespace galera
{
    //!
    // @class Certification
    //
    // @brief Abstract interface for certification implementations
    //
    class Certification
    {
    public:

        //!
        // @brief Assign initial position
        //
        // @param seqno TO Seqno to start from
        //
        virtual void assign_initial_position(wsrep_seqno_t seqno) = 0;

        //!
        // @brief Create transaction for certification.
        //
        // @param data Serialized form of transaction
        // @param data_len Length of serialized transaction
        // @param seqno_l Local seqno
        // @param seqno_g Global seqno
        //
        // @return TrxHandlePtr
        //
        virtual TrxHandlePtr create_trx(const void* data, size_t data_len,
                                        wsrep_seqno_t seqno_l,
                                        wsrep_seqno_t seqno_g) = 0;
        
        //!
        // @brief Append new transaction into certification
        //
        // @param trx Transaction to be appended
        //
        // @return WSDB_OK if successfully appended and passed certification
        //         test, otherwise error
        //
        // @todo Should certification part be refactored out of this call
        //       and require that test() method is called for certification
        //       verdict?
        virtual int append_trx(TrxHandlePtr trx) = 0;
        
        //!
        // @brief Perform certification test for write set
        //
        // @return WSDB_OK if certification test passed, otherwise error code
        //
        virtual int test(const TrxHandlePtr&, bool) = 0;

        //!
        // @brief Get sequence number that is guaranteed to be 
        //        unreferenced by any preceding transaction
        //
        // @return Safe to discard seqno
        //
        virtual wsrep_seqno_t get_safe_to_discard_seqno() const = 0;

        //!
        // @brief Remove transactions' write sets from transaction index
        //
        // After purge only transactions with seqno higher than seqno 
        // are maintainend in certification index and participate in
        // certification process. Precondition, seqno must higher or 
        // equal to safe to discard seqno.
        //
        // @param seqno Sequence number
        //
        virtual void purge_trxs_upto(wsrep_seqno_t seqno) = 0;

        //!
        // @brief Mark transaction committed
        //
        // @param trx
        //
        //
        virtual void set_trx_committed(const TrxHandlePtr& trx) = 0;

        //!
        // @brief Get transaction matching for seqno from certification
        //        index
        //
        // @param seqno Sequence number
        //
        // @return TrxHandlePtr pointing to transaction or object comparable
        //         to zero if not found.
        //
        virtual TrxHandlePtr get_trx(wsrep_seqno_t seqno) = 0;
        
        //!
        // @brief Dereference seqno
        //
        // @todo This should probably be part of TrxHandle
        //
        virtual void deref_seqno(wsrep_seqno_t seqno) = 0;
        
        //!
        // @brief Virtual destructor to allow inheritance
        //
        virtual ~Certification() { }
        
        //!
        // @brief Factory method
        //
        static Certification* create(const std::string& conf);
    protected:
        Certification() { }
    private:
        Certification(const Certification&);
        void operator=(const Certification&);
    };
}

#endif // GALERA_CERTIFICATION_HPP
