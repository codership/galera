// Copyright (C) 2021 Codership Oy <info@codership.com>

#include "gu_signals.hpp"

#include <mutex>

gu::Signals& gu::Signals::Instance()
{
   static gu::Signals instance;
   return instance;
}
gu::Signals::signal_connection gu::Signals::connect(
    const gu::Signals::slot_type &subscriber)
{
    return signal_.connect(subscriber);
}
void gu::Signals::signal(const gu::Signals::SignalType& type)
{
    signal_(type);
}
