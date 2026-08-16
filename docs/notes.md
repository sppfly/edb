It seems like we can never mix OLAP workload with OLTP's. Because the way to organize data is totally different. One is row based and another one is column based. Once you load data to memory, it will be differnent, and the way the execution engine works is decided by the way to organize data. 

For EDB, our aim is to build a database that is totally plugable, you can switch IO backend. 