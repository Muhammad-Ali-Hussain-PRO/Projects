CREATE TABLE customers (customer_id INTEGER PRIMARY KEY, name TEXT, city TEXT);
CREATE TABLE orders (order_id INTEGER PRIMARY KEY, customer_id INTEGER, total NUMERIC, FOREIGN KEY(customer_id) REFERENCES customers(customer_id));
