
package com.codership.galera.jdbc;

import java.sql.SQLException;
import java.sql.SQLClientInfoException;

import java.sql.DriverPropertyInfo;
import java.sql.DriverManager;
import java.sql.Statement;
import java.sql.DatabaseMetaData;
import java.sql.SQLWarning;
import java.sql.CallableStatement;
import java.sql.PreparedStatement;
import java.sql.Savepoint;
import java.sql.Clob;
import java.sql.NClob;
import java.sql.Blob;
import java.sql.Array;
import java.sql.Struct;
import java.sql.SQLXML;

import java.util.Properties;
import java.util.Map;

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
    java.sql.Driver realDriver = null;
    static HostList hosts = null;

    class Connection implements java.sql.Connection
    {
        java.sql.Connection conn = null;
        String host = null;
        
        public Connection(String host, java.sql.Connection conn)
        {
            this.host = host;
            this.conn = conn;
            hosts.increment(host);
        }
        
        public Connection(java.sql.Connection conn)
        {
            this.conn = conn;
        }

        public Properties getClientInfo() throws SQLException
        {
            return conn.getClientInfo();
        }

        public void setClientInfo(Properties properties)
                throws SQLClientInfoException
        {
            conn.setClientInfo(properties);
        }

        public String getClientInfo(String name) throws SQLException
        {
            return conn.getClientInfo(name);
        }

        public void setClientInfo(String name, String value)
                throws SQLClientInfoException
        {
            conn.setClientInfo(name, value);
        }

        public void clearWarnings() throws SQLException
        {
            conn.clearWarnings();
        }

        public void close() throws SQLException
        {
            if (host != null) {
                hosts.decrement(host);
                host = null;
            }
            conn.close();
        }

        public void commit() throws SQLException
        {
            conn.commit();
        }

        public Statement createStatement() throws SQLException
        {
            return conn.createStatement();
        }

        public Statement createStatement(int resultSetType,
                int resultSetConcurrency) throws SQLException
        {
            return conn.createStatement(resultSetType, resultSetConcurrency);
        }

        public Statement createStatement(int resultSetType,
                int resultSetConcurrency, int resultSetHoldability)
                throws SQLException
        {
            return conn.createStatement(resultSetType, resultSetConcurrency,
                    resultSetHoldability);
        }

        public Struct createStruct(String typeName, Object[] attributes)
                throws SQLException
        {
            return conn.createStruct(typeName, attributes);
        }

        public boolean getAutoCommit() throws SQLException
        {
            return conn.getAutoCommit();
        }

        public String getCatalog() throws SQLException
        {
            return conn.getCatalog();
        }

        public int getHoldability() throws SQLException
        {
            return conn.getHoldability();
        }

        public DatabaseMetaData getMetaData() throws SQLException
        {
            return conn.getMetaData();
        }

        public int getTransactionIsolation() throws SQLException
        {
            return conn.getTransactionIsolation();
        }

        public Map<String, Class<?>> getTypeMap() throws SQLException
        {
            return conn.getTypeMap();
        }

        public SQLWarning getWarnings() throws SQLException
        {
            return conn.getWarnings();
        }

        public boolean isClosed() throws SQLException
        {
            return conn.isClosed();
        }

        public boolean isReadOnly() throws SQLException
        {
            return conn.isReadOnly();
        }

        public boolean isValid(int timeout) throws SQLException
        {
            return conn.isValid(timeout);
        }

        public String nativeSQL(String sql) throws SQLException
        {
            return conn.nativeSQL(sql);
        }

        public CallableStatement prepareCall(String sql) throws SQLException
        {
            return conn.prepareCall(sql);
        }

        public CallableStatement prepareCall(String sql, int resultSetType,
                int resultSetConcurrency) throws SQLException
        {
            return conn.prepareCall(sql, resultSetType, resultSetConcurrency);
        }

        public CallableStatement prepareCall(String sql, int resultSetType,
                int resultSetConcurrency, int resultSetHoldability)
                throws SQLException
        {
            return conn.prepareCall(sql, resultSetType, resultSetConcurrency,
                    resultSetHoldability);
        }

        public PreparedStatement prepareStatement(String sql)
                throws SQLException
        {
            return conn.prepareStatement(sql);
        }

        public PreparedStatement prepareStatement(String sql,
                int autoGeneratedKeys) throws SQLException
        {
            return conn.prepareStatement(sql, autoGeneratedKeys);
        }

        public PreparedStatement prepareStatement(String sql,
                int[] columnIndexes) throws SQLException
        {
            return conn.prepareStatement(sql, columnIndexes);
        }

        public PreparedStatement prepareStatement(String sql,
                int resultSetType, int resultSetConcurrency)
                throws SQLException
        {
            return conn.prepareStatement(sql, resultSetType,
                    resultSetConcurrency);
        }

        public PreparedStatement prepareStatement(String sql,
                int resultSetType, int resultSetConcurrency,
                int resultSetHoldability) throws SQLException
        {
            return conn.prepareStatement(sql, resultSetType,
                    resultSetConcurrency, resultSetHoldability);
        }

        public PreparedStatement prepareStatement(String sql,
                String[] columnNames) throws SQLException
        {
            return conn.prepareStatement(sql, columnNames);
        }

        public void releaseSavepoint(Savepoint savepoint) throws SQLException
        {
            conn.releaseSavepoint(savepoint);
        }

        public void rollback() throws SQLException
        {
            conn.rollback();
        }

        public void rollback(Savepoint savepoint) throws SQLException
        {
            conn.rollback(savepoint);
        }

        public void setAutoCommit(boolean autoCommit) throws SQLException
        {
            conn.setAutoCommit(autoCommit);
        }

        public void setCatalog(String catalog) throws SQLException
        {
            conn.setCatalog(catalog);
        }

        public void setHoldability(int holdability) throws SQLException
        {
            conn.setHoldability(holdability);
        }

        public void setReadOnly(boolean readOnly) throws SQLException
        {
            conn.setReadOnly(readOnly);
        }

        public Savepoint setSavepoint() throws SQLException
        {
            return conn.setSavepoint();
        }

        public Savepoint setSavepoint(String name) throws SQLException
        {
            return conn.setSavepoint(name);
        }

        public void setTransactionIsolation(int level) throws SQLException
        {
            conn.setTransactionIsolation(level);
        }

        public void setTypeMap(Map<String, Class<?>> map) throws SQLException
        {
            conn.setTypeMap(map);
        }

        public NClob createNClob() throws SQLException
        {
            return conn.createNClob();
        }

        public Clob createClob() throws SQLException
        {
            return conn.createClob();
        }

        public SQLXML createSQLXML() throws SQLException
        {
            return conn.createSQLXML();
        }

        public Blob createBlob() throws SQLException
        {
            return conn.createBlob();
        }

        public Array createArrayOf(String typeName, Object[] elements)
                throws SQLException
        {
            return conn.createArrayOf(typeName, elements);
        }

        public boolean isWrapperFor(Class<?> iface) throws SQLException
        {
            return conn.isWrapperFor(iface);
        }

        public <T> T unwrap(Class<T> iface) throws SQLException
        {
            return unwrap(iface);
        }
    }

    public Driver() throws Exception
    {
        String hstr = System.getProperty("com.codership.galera_hosts");
        hosts = new HostList(hstr);
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
        String host = null;
        if ((host = hosts.getNextHost()) == null)
        {
            ret = new Connection(realDriver.connect(uri.replace("galera:", ""),
                    prop));
        }
        else
        {
            ret = new Connection(host, realDriver.connect(uri.replace(
                    "<galera-host>", host).replace("galera:", ""), prop));
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