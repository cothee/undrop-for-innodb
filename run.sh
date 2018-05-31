#!/bin/bash

table_name=table1
primary_index=""
data_dir="./data"
pages_dir="./pages"

 #create page files with ibd file
./bin/stream_parser -f ${data_dir}/${table_name}.ibd  -d ${pages_dir}/pages_${table_name}.ibd

function primary_file() {
  for file in `ls $1`
    do
      deal_file=$1"/"${file}
      if test -f ${deal_file}
        then
          #echo ${deal_file}
          primary_index=${deal_file}
          return 1
      fi
   done
   return 0
}

primary_file ${pages_dir}/pages_${table_name}.ibd/FIL_PAGE_INDEX

##-U for undelete file only & -D for deleted file only & -A for all file in
##brute force mode

./bin/c_parser -Uf ${primary_index}  \
 -t ./${data_dir}/${table_name}.sql \
1>dumps/default/${table_name} \
2>dumps/default/${table_name}_load.sql

