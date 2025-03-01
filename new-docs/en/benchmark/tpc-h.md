---
{
    "title": "TPC-H Benchmark Test",
    "language": "en"
}
---

<!--
Licensed to the Apache Software Foundation (ASF) under one
or more contributor license agreements.  See the NOTICE file
distributed with this work for additional information
regarding copyright ownership.  The ASF licenses this file
to you under the Apache License, Version 2.0 (the
"License"); you may not use this file except in compliance
with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied.  See the License for the
specific language governing permissions and limitations
under the License.
-->

# TPC-H Benchmark

[Star Schema Benchmark(SSB)](https://www.cs.umb.edu/~poneil/StarSchemaB.PDF) is a lightweight data warehouse scenario performance test set. Based on [TPC-H](http://www.tpc.org/tpch/), SSB provides a simplified version of the star model data set, which is mainly used to test the performance of multi-table association queries under the star model.

This document mainly introduces how to pass the preliminary performance test of the SSB process in Doris.

> Note 1: The standard test set including SSB is usually far from the actual business scenario, and some tests will perform parameter tuning for the test set. Therefore, the test results of the standard test set can only reflect the performance of the database in a specific scenario. It is recommended that users use actual business data for further testing.
>
> Note 2: The operations involved in this document are all performed in the CentOS 7 environment.

## Environmental preparation

Please refer to the [official document](http://doris.incubator.apache.org/master/en/installing/install-deploy.html) to install and deploy Doris to obtain a normal running Doris cluster ( Contain at least 1 FE, 1 BE).

The scripts involved in the following documents are all stored under `tools/ssb-tools/` in the Doris code base.

## data preparation

### 1. Download and install the SSB data generation tool.

Execute the following script to download and compile the [ssb-dbgen](https://github.com/electrum/ssb-dbgen.git) tool.

```
sh build-ssb-dbgen.sh
```

After the installation is successful, the `dbgen` binary file will be generated in the `ssb-dbgen/` directory.

### 2. Generate SSB test set

Execute the following script to generate the SSB data set:

```
sh gen-ssb-data.sh -s 100 -c 100
```

> Note 1: Run `sh gen-ssb-data.sh -h` for help.
>
> Note 2: The data will be generated under the directory `ssb-data/` with a suffix of `.tbl`. The total file size is about 60GB. The generation time may vary from a few minutes to an hour.
>
> Note 3: `-s 100` means that the test set size factor is 100, `-c 100` means that 100 threads concurrently generate data in the lineorder table. The `-c` parameter also determines the number of files in the final lineorder table. The larger the parameter, the more files and the smaller each file.

Under the `-s 100` parameter, the generated data set size is:

|Table |Rows |Size | File Number |
|---|---|---|---|
|lineorder| 600 million (600037902) | 60GB | 100|
|customer|30 million (3000000) |277M |1|
|part|1.4 million (1400000) | 116M|1|
|supplier|200,000 (200,000) |17M |1|
|date| 2556|228K |1|

3. Build a table

    Copy the table creation statement in [create-tables.sql](https://github.com/apache/incubator-doris/tree/master/tools/ssb-tools/create-tables.sql) and execute it in Doris.

4. Import data

    0. Prepare the 'doris-cluster.conf' file.

         Before calling the load script, you need to write the FE's ip port and other information in the `doris-cluster.conf` file.

         'doris-cluster.conf' in the same directory as `load-dimension-data.sh`.

         The contents of the file include FE's ip, HTTP port, user name, password and the DB name of the data to be loaded:

         ````
         export FE_HOST="xxx"
         export FE_HTTP_PORT="8030"
         export USER="root"
         export PASSWORD='xxx'
         export DB="ssb"
         ````

    1. Load 4 dimension table data (customer, part, supplier and date)

        Because the data volume of these 4 dimension tables is small, and the load is simpler, we use the following command to load the data of these 4 tables first:

        `sh load-dimension-data.sh`

    2. Load the fact table lineorder.

        Load the lineorder table data with the following command:

        `sh load-fact-data.sh -c 5`

        `-c 5` means to start 5 concurrent threads to import (the default is 3). In the case of a single BE node, the load time of lineorder data generated by `sh gen-ssb-data.sh -s 100 -c 100` using `sh load-fact-data.sh -c 3` is about 10 minutes. The memory overhead is about 5-6GB. If you turn on more threads, you can speed up the load speed, but it will increase additional memory overhead.

        > Note: To get a faster import speed, you can add `flush_thread_num_per_store=5` in be.conf and restart BE. This configuration indicates the number of disk write threads for each data directory, and the default is 2. Larger data can increase write data throughput, but may increase IO Util. (Reference value: 1 mechanical disk, when the default is 2, the IO Util during the import process is about 12%, when it is set to 5, the IO Util is about 26%. If it is an SSD disk, it is almost 0) .

5. Check the loaded data

    ```
    select count(*) from part;
    select count(*) from customer;
    select count(*) from supplier;
    select count(*) from date;
    select count(*) from lineorder;
    ```

    The amount of data should be the same as the number of rows of generated data.

## Query test

There are 4 groups of 14 SQL in the SSB test set. The query statement is in the [queries/](https://github.com/apache/incubator-doris/tree/master/tools/ssb-tools/queries) directory.

## testing report

The following test report is based on Doris [branch-0.15](https://github.com/apache/incubator-doris/tree/branch-0.15) branch code test, for reference only. (Update time: October 25, 2021)

1. Hardware environment

    * 1 FE + 1-3 BE mixed
    * CPU: 96core, Intel(R) Xeon(R) Gold 6271C CPU @ 2.60GHz
    * Memory: 384GB
    * Hard disk: 1 HDD
    * Network card: 10 Gigabit network card

2. Data set

    |Table |Rows |Origin Size | Compacted Size(1 Replica) |
    |---|---|---|---|
    |lineorder| 600 million (600037902) | 60 GB | 14.846 GB |
    |customer|30 million (3000000) |277 MB | 414.741 MB |
    |part|1.4 million (1.400000) | 116 MB | 38.277 MB |
    |supplier|200,000 (200,000) |17 MB | 27.428 MB |
    |date| 2556|228 KB | 275.804 KB |

3. Test results

    |Query |Time(ms) (1 BE) | Time(ms) (3 BE) | Parallelism | Runtime Filter Mode |
    |---|---|---|---|---|
    | q1.1 | 200 | 140 | 8 | IN |
    | q1.2 | 90 | 80 | 8 | IN |
    | q1.3 | 90 | 80 | 8 | IN |
    | q2.1 | 1100 | 400 | 8 | BLOOM_FILTER |
    | q2.2 | 900 | 330 | 8 | BLOOM_FILTER |
    | q2.3 | 790 | 320 | 8 | BLOOM_FILTER |
    | q3.1 | 3100 | 1280 | 8 | BLOOM_FILTER |
    | q3.2 | 700 | 270 | 8 | BLOOM_FILTER |
    | q3.3 | 540 | 270 | 8 | BLOOM_FILTER |
    | q3.4 | 560 | 240 | 8 | BLOOM_FILTER |
    | q4.1 | 2820 | 1150 | 8 | BLOOM_FILTER |
    | q4.2 | 1430 | 670 | 8 | BLOOM_FILTER |
    | q4.2 | 1750 | 1030 | 8 | BLOOM_FILTER |

    > Note 1: "This test set is far from your production environment, please be skeptical!"
    >
    > Note 2: The test result is the average value of multiple executions (Page Cache will play a certain acceleration role). And the data has undergone sufficient compaction (if you test immediately after importing the data, the query delay may be higher than the test result)
    >
    > Note 3: Due to environmental constraints, the hardware specifications used in this test are relatively high, but so many hardware resources will not be consumed during the entire test. The memory consumption is within 10GB, and the CPU usage is within 10%.
    >
    > Note 4: Parallelism means query concurrency, which is set by `set parallel_fragment_exec_instance_num=8`.
    >
    > Note 5: Runtime Filter Mode is the type of Runtime Filter, set by `set runtime_filter_type="BLOOM_FILTER"`. ([Runtime Filter](http://doris.incubator.apache.org/master/en/administrator-guide/runtime-filter.html) function has a significant effect on the SSB test set. Because in this test set, The data from the right table of Join can filter the left table very well. You can try to turn off this function through `set runtime_filter_mode=off` to see the change in query latency.)
