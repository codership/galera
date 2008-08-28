package com.codership.galera.jdbc;

class HostList
{
    String[] hosts = null;
    int      cur   = 0;
    int[]    cnt   = null;

    public HostList(String hstr)
    {
        hosts = hstr.split(",");
        cnt = new int[hosts.length];
        for (int i = 0; i < cnt.length; ++i)
            cnt[i] = 0;
    }

    public String getNextHost()
    {
        if (hosts.length == 0)
            return null;
        String host = null;
        synchronized (this)
        {
            host = hosts[cur];
            cur = (cur + 1) % hosts.length;
        }
        return host;
    }
    public void increment(String host)
    {
        // TODO
        // System.err.println("Hosts increment " + host);
    }
    public void decrement(String host)
    {
        // TODO
        // System.err.println("Hosts decrement " + host);
    }
}

