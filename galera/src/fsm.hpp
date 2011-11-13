//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#ifndef GALERA_FSM_HPP
#define GALERA_FSM_HPP

#include "gu_unordered.hpp"
#include "gu_throw.hpp"
#include <list>
#include <vector>

namespace galera
{
    class EmptyGuard
    {
    public:
        bool operator()() const { return true; }
    };
    class EmptyAction
    {
    public:
        void operator()() { }
    };

    template <class State,
              class Transition,
              class Guard  = EmptyGuard,
              class Action = EmptyAction>
    class FSM
    {
    public:
        class TransAttr
        {
        public:
            TransAttr()
                :
                pre_guard_(0),
                post_guard_(0),
                pre_action_(0),
                post_action_(0)
            { }
            std::list<Guard> pre_guard_;
            std::list<Guard> post_guard_;
            std::list<Action> pre_action_;
            std::list<Action> post_action_;
        };
        typedef gu::UnorderedMap<Transition, TransAttr, typename Transition::Hash> TransMap;

        FSM(State const initial_state)
            :
            delete_(true),
            trans_map_(new TransMap),
            state_(initial_state),
            state_hist_()
        { }

        FSM(TransMap* const trans_map, State const initial_state)
            :
            delete_(false),
            trans_map_(trans_map),
            state_(initial_state),
            state_hist_()
        { }

        ~FSM()
        {
            if (delete_ == true) delete trans_map_;
        }

        void shift_to(State const state)
        {
            typename TransMap::iterator
                i(trans_map_->find(Transition(state_, state)));
            if (i == trans_map_->end())
            {
                log_fatal << "FSM: no such a transition "
                          << state_ << " -> " << state;
//                gu_throw_fatal << "FSM: no such a transition "
//                               << state_ << " -> " << state;
                abort(); // we want to catch it in the stack
            }

            typename std::list<Guard>::const_iterator gi;
            for (gi = i->second.pre_guard_.begin();
                 gi != i->second.pre_guard_.end(); ++gi)
            {
                if ((*gi)() == false)
                {
                    log_fatal << "FSM: pre guard failed for "
                              << state_ << " -> " << state;
                    gu_throw_fatal << "FSM: pre guard failed for "
                                   << state_ << " -> " << state;
                }
            }

            typename std::list<Action>::iterator ai;
            for (ai = i->second.pre_action_.begin();
                 ai != i->second.pre_action_.end(); ++ai)
            {
                (*ai)();
            }

            state_hist_.push_back(state_);
            state_ = state;

            for (ai = i->second.post_action_.begin();
                 ai != i->second.post_action_.end(); ++ai)
            {
                (*ai)();
            }

            for (gi = i->second.post_guard_.begin();
                 gi != i->second.post_guard_.end(); ++gi)
            {
                if ((*gi)() == false)
                {
                    log_fatal << "FSM: post guard failed for "
                              << state_ << " -> " << state;
                    gu_throw_fatal << "FSM: post guard failed for "
                                   << state_ << " -> " << state;
                }
            }
        }

        const State& operator()() const { return state_; }

        void add_transition(Transition const& trans)
        {
            if (trans_map_->insert(
                    std::make_pair(trans, TransAttr())).second == false)
            {
                gu_throw_fatal << "transition "
                               << trans.from() << " -> " << trans.to()
                               << " already exists";
            }
        }

        void add_pre_guard(Transition const& trans, Guard const& guard)
        {
            typename TransMap::iterator i(trans_map_->find(trans));
            if (i == trans_map_->end())
            {
                gu_throw_fatal << "no such a transition "
                               << trans.from() << " -> " << trans.to();
            }
            i->second.pre_guard_.push_back(guard);
        }

        void add_post_guard(Transition const& trans, Guard const& guard)
        {
            typename TransMap::iterator i(trans_map_->find(trans));
            if (i == trans_map_->end())
            {
                gu_throw_fatal << "no such a transition "
                               << trans.from() << " -> " << trans.to();
            }
            i->second.post_guard_.push_back(guard);
        }

        void add_pre_action(Transition const& trans, Action const& action)
        {
            typename TransMap::iterator i(trans_map_->find(trans));
            if (i == trans_map_->end())
            {
                gu_throw_fatal << "no such a transition "
                               << trans.from() << " -> " << trans.to();
            }
            i->second.pre_action_.push_back(action);
        }

        void add_post_action(Transition const& trans, Action const& action)
        {
            typename TransMap::iterator i(trans_map_->find(trans));
            if (i == trans_map_->end())
            {
                gu_throw_fatal << "no such a transition "
                               << trans.from() << " -> " << trans.to();
            }
            i->second.post_action_.push_back(action);
        }

    private:

        FSM(const FSM&);
        void operator=(const FSM&);

        bool delete_;
        TransMap* const trans_map_;
        State state_;
        std::vector<State> state_hist_;
    };

}

#endif // GALERA_FSM_HPP
