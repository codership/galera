
package com.codership.galera.jdbc;

import java.sql.SQLException;
import java.sql.Connection;
import java.sql.DriverPropertyInfo;
import java.sql.DriverManager;
import java.util.Properties;

/*
 * System parameters:
 * - com.codership.galera_hosts: Comma separated list of hostnames
 * - com.codership.galera_dbms_driver: Name of real jdbc driver
 *
 * Uri format is 'jdbc:galera:mysql://<galera-host>/?user=test&password=testpass'
 * where <galera-host> tag is replaced with hostname in galera hosts list
 * in round robin fashion.
 */
class Driver implements java.sql.Driver
{
    static String   hosts[];
    static int      cur        = 0;
    java.sql.Driver realDriver = null;

    public Driver() throws Exception
    {
        String hstr = System.getProperty("com.codership.galera_hosts");
        if (hstr != null)
        {
            hosts = hstr.split(",");
        }
        realDriver = (java.sql.Driver) Class.forName(
                System.getProperty("com.codership.galera_dbms_driver"))
                .newInstance();
        System.err.println("Driver loaded: " + realDriver.getClass().getName());
        DriverManager.registerDriver(this);
    }

    public Connection connect(String uri, Properties prop) throws SQLException
    {
        // System.err.println("connect(" + uri + ")");
        Connection ret = null;
        if (hosts.length == 0)
        {
            ret = realDriver.connect(uri.replace("galera:", ""), prop);
        }
        else
        {
            String realUri = null;
            synchronized (this)
            {
                realUri = uri.replaceFirst("<galera-host>", hosts[cur])
                        .replace("galera:", "");
                System.err.println("URI: " + realUri);
                cur = (cur + 1) % hosts.length;
            }
            ret = realDriver.connect(realUri, prop);
        }
        // System.err.println("Return: " + ret);
        return ret;

    }

    public boolean acceptsURL(String url) throws SQLException
    {
        if (url.startsWith("jdbc:galera:"))
            return realDriver.acceptsURL(url.replace("galera:", ""));
        return false;
    }

    public int getMajorVersion()
    {
        return realDriver.getMajorVersion();
    }

    public int getMinorVersion()
    {
        return realDriver.getMinorVersion();
    }

    public DriverPropertyInfo[] getPropertyInfo(String url, Properties prop)
            throws SQLException
    {
        return realDriver.getPropertyInfo(url, prop);
    }

    public boolean jdbcCompliant()
    {
        return realDriver.jdbcCompliant();
    }
}