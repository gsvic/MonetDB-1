START TRANSACTION;
COPY INTO region from 'PWD/region.tbl' ON CLIENT USING DELIMITERS '|', E'|\n';
COPY INTO nation from 'PWD/nation.tbl' ON CLIENT USING DELIMITERS '|', E'|\n';
COPY INTO supplier from 'PWD/supplier.tbl' ON CLIENT USING DELIMITERS '|', E'|\n';
COPY INTO customer from 'PWD/customer.tbl' ON CLIENT USING DELIMITERS '|', E'|\n';
COPY INTO part from 'PWD/part.tbl' ON CLIENT USING DELIMITERS '|', E'|\n';
COPY INTO partsupp from 'PWD/partsupp.tbl' ON CLIENT USING DELIMITERS '|', E'|\n';
COPY INTO orders from 'PWD/orders.tbl' ON CLIENT USING DELIMITERS '|', E'|\n';
COPY INTO lineitem from 'PWD/lineitem.tbl' ON CLIENT USING DELIMITERS '|', E'|\n';
COMMIT;
