package com.codership.galera.jdbc;

import java.sql.SQLException;
import java.sql.Connection;
import java.sql.DriverPropertyInfo;
import java.sql.DriverManager;
import java.util.Properties;

class Driver implements java.sql.Driver
{
    static String hosts[];
    static int cur = 0;
    java.sql.Driver realDriver = null;
    
    public Driver() throws Exception
    {
        String hstr = System.getProperty("com.codership.galera_hosts");
        if (hstr != null)
        {
            hosts = hstr.split(",");
        }
        realDriver = (java.sql.Driver)Class.forName("com.mysql.jdbc.Driver").newInstance();
        System.err.println("Driver loaded: " + realDriver.getClass().getName());
        DriverManager.registerDriver(this);
    }

    public Connection connect(String uri, Properties prop) throws SQLException
    {
        // System.err.println("connect(" + uri + ")");
        Connection ret = null;
        if (hosts.length == 0) {
            ret = realDriver.connect(uri.replace("galera:", ""), prop);
        } else {
            String realUri = uri.replaceFirst("<galera-host>", hosts[cur]);
           realUri = realUri.replace("galera:", "");
            System.err.println("URI: " + realUri);
            cur = (cur + 1) % hosts.length;
            ret = realDriver.connect(realUri, prop);
        }
        // System.err.println("Return: " + ret);
        return ret;
        
    }
    
    public boolean acceptsURL(String url) throws SQLException
    {
        System.err.println("acceptsURL()");
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
    
    public DriverPropertyInfo[] getPropertyInfo(String url, Properties prop) throws SQLException
    {
        return realDriver.getPropertyInfo(url, prop);
    }
    
    public boolean jdbcCompliant()
    {
        return realDriver.jdbcCompliant();
    }
}