-- comment
SELECT name, COUNT(*) AS n FROM users WHERE active = true AND age > 18 GROUP BY name;
