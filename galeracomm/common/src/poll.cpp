#include <galeracomm/poll.hpp>

/**
 * How soon connection should be pronounced dead after not receiving anything
 * from peer (should be configurable?)
 */
int const Poll::DEFAULT_KA_TIMEOUT  = 4000;

/**
 * How often keepalives should be sent in the absence of other traffic
 */
int const Poll::DEFAULT_KA_INTERVAL = Poll::DEFAULT_KA_TIMEOUT / 8;
