import unittest
from studio.analyst import run,execute,SAMPLE
class AnalystTests(unittest.TestCase):
    def test_aggregate(self):
        r=run({'question':'total revenue by region'})
        self.assertEqual(r['rows'][0],['North',2000.0]);self.assertEqual(sum(x[1] for x in r['rows']),6900)
    def test_write_denied(self):
        for sql in ['DROP TABLE data','SELECT load_extension("x")','SELECT * FROM sqlite_master','SELECT 1; DELETE FROM data']:
            with self.assertRaises(ValueError):execute(SAMPLE,sql)
    def test_csv_schema(self):
        with self.assertRaises(ValueError):run({'csv':'x,x\n1,2'})
    def test_non_numeric(self):
        self.assertEqual(run({'csv':'team\nA\nB\n','question':'count rows'})['rows'],[[2]])
    def test_empty_unknown(self):
        with self.assertRaises(ValueError):run({'question':'why is revenue falling?'})
    def test_expensive_query(self):
        with self.assertRaises(ValueError):execute(SAMPLE,'WITH RECURSIVE t(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM t) SELECT sum(n) FROM t')
