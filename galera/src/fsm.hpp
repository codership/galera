//
// Copyright (C) 2010-2021 Codership Oy <info@codership.com>
//

#ifndef GALERA_FSM_HPP
#define GALERA_FSM_HPP

#include "gu_unordered.hpp"
#include "gu_throw.hpp"
#include <list>
#include <vector>

namespace galera
{
    template <class State,
              class Transition>
    class FSM
    {
    public:

        typedef gu::UnorderedSet<Transition,
                                 typename Transition::Hash> TransMap;

        typedef std::pair<State, int> StateEntry;

        FSM(State const initial_state)
            :
            delete_(true),
            trans_map_(new TransMap),
            state_(initial_state, 0),
            state_hist_()
        { }

        FSM(TransMap* const trans_map, State const initial_state)
            :
            delete_(false),
            trans_map_(trans_map),
            state_(initial_state, 0),
            state_hist_()
        { }

        ~FSM()
        {
            if (delete_ == true) delete trans_map_;
        }

        void shift_to(State const state, int const line = -1)
        {
            auto i = trans_map_->find(Transition(state_.first, state));
            if (i == trans_map_->end())
            {
                log_fatal << "FSM: no such a transition "
                          << state_.first << " -> " << state;
                abort(); // we want to catch it in the stack
            }

            StateEntry const se(state, line);
            state_hist_.push_back(state_);
            state_ = se;
        }

        void force(State const state)
        {
            state_ = StateEntry(state, 0);
        }

        void reset_history()
        {
            state_hist_.clear();
        }

        const State& operator()() const { return state_.first; }
        const StateEntry& get_state_entry() const { return state_; }

        void add_transition(Transition const& trans)
        {
            if (trans_map_->insert(trans).second == false)
            {
                gu_throw_fatal << "transition "
                               << trans.from() << " -> " << trans.to()
                               << " already exists";
            }
        }

        const std::vector<StateEntry>& history() const { return state_hist_; }

    private:

        FSM(const FSM&);
        void operator=(const FSM&);

        bool delete_;
        TransMap* const trans_map_;

        StateEntry state_;
        std::vector<StateEntry> state_hist_;
    };

}

#endif // GALERA_FSM_HPP
