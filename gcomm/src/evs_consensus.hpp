/*
 * Copyright (C) 2009-2014 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_EVS_CONSENSUS_HPP
#define GCOMM_EVS_CONSENSUS_HPP

#include "evs_seqno.hpp"


namespace gcomm
{
    class UUID;
    class View;
    namespace evs
    {
        class NodeMap;
        class InputMap;
        class Message;
        class Consensus;
        class Proto;
    }
}

class gcomm::evs::Consensus
{
public:
    Consensus(const Proto&    proto,
              const NodeMap&  known,
              const InputMap& input_map,
              const View&     current_view) :
        proto_       (proto),
        known_       (known),
        input_map_   (input_map),
        current_view_(current_view)
    { }

    /*!
     * Compare two messages if they are equal in consensus context.
     */
    bool equal(const Message&, const Message&) const;

    /*!
     * Compute highest reachable safe seq from local state.
     *
     * @return Highest reachable safe seq.
     */
    seqno_t highest_reachable_safe_seq() const;

    // input map safe seq but without considering
    // all suspected leaving nodes.
    seqno_t safe_seq_wo_all_susupected_leaving_nodes() const;

    /*!
     * Check if highest reachable safe seq according to message
     * consistent with local state.
     */
    bool is_consistent_highest_reachable_safe_seq(const Message&) const;

    /*!
     * Check if message aru seq, safe seq and node ranges matches to
     * local state.
     */
    bool is_consistent_input_map(const Message&) const;
    bool is_consistent_partitioning(const Message&) const;
    bool is_consistent_leaving(const Message&) const;
    bool is_consistent_same_view(const Message&) const;
    bool is_consistent(const Message&) const;
    bool is_consensus() const;
private:

    const Proto&    proto_;
    const NodeMap&  known_;
    const InputMap& input_map_;
    const View&     current_view_;
};

#endif // GCOMM_EVS_CONSENSUS_HPP
