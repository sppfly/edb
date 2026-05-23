CREATE TABLE users (id INTEGER, name TEXT, active BOOLEAN);
INSERT INTO users VALUES (1, 'alice', TRUE);
INSERT INTO users VALUES (2, 'bob', FALSE);
SELECT id, name FROM users WHERE active = TRUE;