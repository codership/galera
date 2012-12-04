/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

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
    }
}

class gcomm::evs::Consensus
{
public:
    Consensus(const UUID&     uuid,
              const NodeMap&  known,
              const InputMap& input_map,
              const View&     current_view) :
        uuid_        (uuid),
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

    const UUID& uuid() const { return uuid_; }

    const UUID&     uuid_;
    const NodeMap&  known_;
    const InputMap& input_map_;
    const View&     current_view_;
};
