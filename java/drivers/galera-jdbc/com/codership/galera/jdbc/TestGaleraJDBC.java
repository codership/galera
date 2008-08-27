
package com.codership.galera.jdbc;

import java.sql.DriverManager;
import java.sql.Connection;

public class TestGaleraJDBC
{
    String     uri  = "jdbc:galera:mysql://<galera-host>/?user=root&password=rootpass";
    Connection conn = null;

    public TestGaleraJDBC() throws Exception
    {
        System.setProperty("com.codership.galera_hosts", "localhost,127.0.0.1");
        System.setProperty("com.codership.galera_dbms_driver", "com.mysql.jdbc.Driver");
        Class.forName("com.codership.galera.jdbc.Driver").newInstance();

    }

    public void connect() throws Exception
    {
        conn = DriverManager.getConnection(uri);
    }

    public void close() throws Exception
    {
        conn.close();
    }

    public static void main(String argv[]) throws Exception
    {
        try
        {
            TestGaleraJDBC test = new TestGaleraJDBC();
            for (int i = 0; i < 4; ++i)
            {
                test.connect();
                test.close();
            }
        }
        catch (Exception e)
        {
            e.printStackTrace();
            System.err.println(e.getMessage());
        }
    }

}
