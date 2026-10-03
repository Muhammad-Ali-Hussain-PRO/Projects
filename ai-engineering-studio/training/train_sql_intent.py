"""Reproduce a small generic-intent model -> SQL intent domain-adaptation experiment.

This is supervised weight adaptation of an eight-way classifier, not an LLM.
The test and development texts never enter fitting or vocabulary construction.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import platform
from pathlib import Path

import numpy as np
import sklearn
from sklearn.feature_extraction.text import TfidfVectorizer
from sklearn.linear_model import SGDClassifier
from sklearn.metrics import accuracy_score, confusion_matrix, f1_score

ROOT = Path(__file__).resolve().parents[1]
LABELS = ["select", "filter", "count", "group", "join", "insert", "update", "delete"]
SQL = {
    "select": "SELECT customer_id, name, city FROM customers LIMIT 100;",
    "filter": "SELECT customer_id, name, city FROM customers WHERE city = :city LIMIT 100;",
    "count": "SELECT COUNT(*) AS customer_count FROM customers;",
    "group": "SELECT c.city, SUM(o.total) AS revenue FROM customers c JOIN orders o ON o.customer_id = c.customer_id GROUP BY c.city;",
    "join": "SELECT c.name, o.order_id, o.total FROM customers c JOIN orders o ON o.customer_id = c.customer_id LIMIT 100;",
    "insert": "INSERT INTO customers (name, city) VALUES (:name, :city);",
    "update": "UPDATE customers SET city = :city WHERE customer_id = :customer_id;",
    "delete": "DELETE FROM customers WHERE customer_id = :customer_id;",
}

# Each row is a manually authored semantic phrasing. Repetition adds distinct
# harmless discourse prefixes; the same semantic task across splits is expected.
GENERIC = {
 "select": ["show the complete address book", "list all saved records", "display the roster", "retrieve every profile", "give me the entire directory", "view all entries", "browse the catalog", "read the full list"],
 "filter": ["find records matching a condition", "show profiles from a particular city", "list entries where status is active", "filter the directory by a chosen field", "retrieve only matching contacts", "search for people from one location", "display records that satisfy a rule", "limit the list to a matching value"],
 "count": ["how many entries exist", "count the records", "return the number of profiles", "give the total number of items", "calculate how many contacts are saved", "report the record count", "measure the number of entries", "tell me the size of the directory"],
 "group": ["summarize totals by category", "aggregate spending per location", "group entries by their department", "calculate a total for each region", "break down the sum by city", "give category level totals", "roll up values for every group", "sum amounts grouped by location"],
 "join": ["link profiles with their related records", "combine contacts and transactions", "match people to their purchases", "join the two collections", "connect each profile to its history", "show related entries beside their owners", "associate members with their bookings", "merge records using their common identifier"],
 "insert": ["add a new contact", "create a record", "insert a new entry", "register a person", "save a new profile", "append an item to the directory", "make a new record", "put a new contact in the list"],
 "update": ["change an existing profile", "update a record", "edit a saved contact", "replace a field value", "modify a person in the directory", "set the location of an existing entry", "correct a stored address", "revise an existing record"],
 "delete": ["remove an existing contact", "delete a record", "erase an entry", "drop a person from the directory", "discard a saved profile", "purge the chosen item", "take a record out of the list", "remove the selected entry"],
}

TARGET = {
 "select": {
  "train": ["list all customers", "show every customer record", "display the customers table", "retrieve all customer names", "give me the customer directory", "view the complete customer list", "browse all customers", "read every customer profile", "show the entire customer roster", "fetch customers without a condition", "list customer names and cities", "display each customer"],
  "dev": ["let me see all customers", "return the full customers directory", "show all names in customers", "retrieve every customer profile", "list the complete customer table", "give all customer names"],
  "test": ["who is in our customer directory", "pull up the whole customer roster", "I need a list of every customer", "display our entire customer directory", "show the complete set of customers", "return each customer name and city", "fetch the full customer list", "read all rows of customers"],
 },
 "filter": {
  "train": ["find customers in a selected city", "show customers where city matches the input", "list customers from a particular city", "filter customers by city", "retrieve customers matching a city", "search customers in London", "display only customers in Paris", "give customers whose city is Berlin", "limit customers to Rome", "show customers with city equal to Madrid", "fetch customers who live in Dublin", "select matching customers in a city"],
  "dev": ["find customer rows for Paris", "return customers located in London", "show only matching customer cities", "filter the customer list to Madrid", "retrieve customers that live in Rome", "display customer profiles from Berlin"],
  "test": ["which customers live in Lisbon", "get customers whose city equals Oslo", "restrict the customer roster to Vienna", "I need customers based in Prague", "show customer rows matching Zurich", "find everyone in customers from Helsinki", "pull customers located in Athens", "display customers with a city of Warsaw"],
 },
 "count": {
  "train": ["count all customers", "how many customers are there", "return the customer count", "give the total number of customers", "calculate the number of customer records", "tell me how many customers exist", "show the count of customers", "report total customer rows", "measure the customer directory size", "get the customer record count", "number of customers please", "count rows in customers"],
  "dev": ["what is the count of customer profiles", "how large is the customer table", "return how many customer rows exist", "total customer count please", "calculate customer table size", "tell me the number of customer entries"],
  "test": ["how many people are recorded as customers", "what number of customer profiles do we have", "count every row in the customer directory", "I need the total customer population", "give me a tally of customer records", "return a count for the whole customers table", "how many entries does customers contain", "report the number of saved customers"],
 },
 "group": {
  "train": ["sum order totals by customer city", "group revenue by city", "aggregate customer spending per city", "show revenue for each city", "break down order totals by city", "summarize order revenue by city", "total order value grouped by city", "calculate sales totals for every city", "roll up revenue per customer city", "give city level spending totals", "sum purchases for each city", "group orders and sum totals by city"],
  "dev": ["give an aggregate of spending by city", "what are sales totals per city", "summarize revenue for each customer city", "calculate grouped order totals by city", "break down spending per city", "return city level order revenue"],
  "test": ["for each city add up the order amounts", "I need a revenue breakdown across cities", "show a sum of sales grouped by customer location", "total the orders separately for each city", "roll up purchase amounts by city", "compute the revenue aggregate per city", "report spending totals for every customer city", "summarize total order value by customer city"],
 },
 "join": {
  "train": ["join customers and orders", "show customers with their orders", "link each customer to their purchases", "combine customer names and order totals", "match orders with customer profiles", "connect customers and order records", "list orders beside their customer names", "show related orders for each customer", "merge customers with orders by customer id", "associate customers with their order history", "retrieve customer names along with orders", "display orders and their owners"],
  "dev": ["give customers alongside their purchases", "connect the orders table to customers", "show names associated with order ids", "match each purchase to a customer", "combine customer information with order records", "link orders to customer names"],
  "test": ["who placed each order", "show a customer's name next to each purchase", "pair the order history with customer profiles", "get order totals together with buyer names", "bring customer and purchase records together", "attach customers to their order rows", "look up each order's customer name", "give order details and the corresponding customers"],
 },
 "insert": {
  "train": ["insert a new customer", "add a customer with name and city", "create a customer record", "register a new customer profile", "save a new customer", "append a customer to the table", "make a customer entry", "put a new customer in the database", "add a new row to customers", "create a customer named Avery", "insert customer name and city values", "register a customer from London"],
  "dev": ["save a fresh customer profile", "add someone to the customers table", "make a new customer record", "insert a customer row with their city", "create another customer profile", "register someone as a customer"],
  "test": ["enroll a new customer named Morgan", "put a fresh customer into customers", "I want to add Casey as a customer", "write a new customer record with name and city", "store a new customer profile", "append another person to customers", "create an entry for a new buyer", "insert one customer into the customer table"],
 },
 "update": {
  "train": ["update a customer's city", "change the city of an existing customer", "edit a customer record", "set the customer's city to Paris", "modify a customer's city field", "replace a saved customer city", "correct a customer's city", "revise an existing customer profile", "update customer by customer id", "change customer city to London", "edit the city for one customer", "set a different city for a customer"],
  "dev": ["correct the city stored for a customer", "modify the customer city by id", "replace the city of a saved customer", "edit an existing customer's location", "update a customer to a new city", "revise one customer's city value"],
  "test": ["move customer 42 to a different city", "make the customer's city read Lisbon", "change where an existing customer lives", "overwrite the city on one customer profile", "fix the location of the selected customer", "set customer 7's city to Oslo", "amend an existing customer's city", "edit a saved customer's city field"],
 },
 "delete": {
  "train": ["delete a customer by id", "remove one customer record", "erase the selected customer", "drop a customer from the table", "discard a saved customer", "purge a customer's profile", "take a customer out of the database", "remove an existing customer", "delete the chosen customer row", "erase customer with a given id", "remove customer 12", "delete one entry in customers"],
  "dev": ["purge the customer identified by id", "discard an existing customer row", "take one person out of customers", "erase a customer's saved profile", "remove the selected customer entry", "delete customer profile by identifier"],
  "test": ["remove customer 42 from our records", "erase the row belonging to customer 7", "I want to delete a saved customer", "take the chosen customer off the roster", "purge one customer from customers", "discard a customer record by its id", "drop the selected customer profile", "delete a single customer entry"],
 },
}


def dataset():
    rows = []
    for label in LABELS:
        for i, phrase in enumerate(GENERIC[label]):
            for j, prefix in enumerate(["", "Please ", "Can you ", "I would like to "]):
                rows.append(dict(id=f"generic-{label}-{i}-{j}", text=prefix + phrase, intent=label, split="pretrain", domain="generic"))
        for split, phrases in TARGET[label].items():
            prefixes = ["", "Please ", "Can you "] if split == "train" else ["", "For this task, "]
            for i, phrase in enumerate(phrases):
                for j, prefix in enumerate(prefixes):
                    rows.append(dict(id=f"sql-{split}-{label}-{i}-{j}", text=prefix + phrase, intent=label, split=split, domain="sql", sql_template=SQL[label]))
    texts = [r["text"].lower().strip() for r in rows]
    if len(texts) != len(set(texts)):
        raise ValueError("Exact duplicate texts in corpus")
    return rows


def fit_epochs(clf, x, y, epochs, seed):
    rng = np.random.default_rng(seed)
    for _ in range(epochs):
        order = rng.permutation(len(y))
        clf.partial_fit(x[order], y[order], classes=np.array(sorted(LABELS)))
    return clf


def new_classifier():
    return SGDClassifier(loss="log_loss", alpha=0.0001, learning_rate="constant", eta0=0.05, random_state=17)


def export_model(path, vectorizer, classifier, phase):
    artifact = dict(format_version=1, model_type="tfidf_sgd_logistic_intent_classifier", phase=phase,
                    vocabulary={k: int(v) for k, v in vectorizer.vocabulary_.items()}, idf=vectorizer.idf_.tolist(),
                    classes=classifier.classes_.tolist(), coef=classifier.coef_.tolist(),
                    intercept=classifier.intercept_.tolist(), sql_templates=SQL,
                    schema={"customers": ["customer_id", "name", "city"], "orders": ["order_id", "customer_id", "total"]},
                    limitations=["Classifier chooses one of eight intents; it does not generate arbitrary SQL.",
                                 "Templates contain unbound named parameters and are never executed.",
                                 "Curated synthetic text with overlapping intents, not production or adversarial coverage."])
    path.write_text(json.dumps(artifact, sort_keys=True), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/model")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    data_dir = ROOT / "data/model"
    data_dir.mkdir(parents=True, exist_ok=True)
    rows = dataset()
    data_path = data_dir / "sql_intent_dataset.jsonl"
    data_path.write_text("".join(json.dumps(r) + "\n" for r in rows), encoding="utf-8")
    splits = {s: [r for r in rows if r["split"] == s] for s in ["pretrain", "train", "dev", "test"]}
    # Vocabulary is frozen before either held-out split is inspected.
    vectorizer = TfidfVectorizer(ngram_range=(1, 2), sublinear_tf=True, max_features=4000)
    vectorizer.fit([r["text"] for r in splits["pretrain"] + splits["train"]])
    xx = {s: vectorizer.transform([r["text"] for r in rs]) for s, rs in splits.items()}
    yy = {s: np.array([r["intent"] for r in rs]) for s, rs in splits.items()}
    base = fit_epochs(new_classifier(), xx["pretrain"], yy["pretrain"], 60, 17)
    export_model(args.output / "base_model.json", vectorizer, base, "generic_pretrained")
    # Copy the actual learned generic parameters; retain the partial_fit step count.
    adapted = new_classifier()
    adapted.classes_ = base.classes_.copy()
    adapted.coef_ = base.coef_.copy()
    adapted.intercept_ = base.intercept_.copy()
    adapted.t_ = base.t_
    adapted.n_features_in_ = base.n_features_in_
    adapted = fit_epochs(adapted, xx["train"], yy["train"], 45, 23)
    scratch = fit_epochs(new_classifier(), xx["train"], yy["train"], 45, 23)
    export_model(args.output / "adapted_model.json", vectorizer, adapted, "sql_domain_adapted")
    np.savez_compressed(args.output / "adapted_weights.npz", coef=adapted.coef_, intercept=adapted.intercept_, idf=vectorizer.idf_, classes=adapted.classes_)
    metrics = {}
    for name, clf in [("generic_base", base), ("sql_from_scratch", scratch), ("sql_adapted", adapted)]:
        metrics[name] = {}
        for split in ["dev", "test"]:
            pred = clf.predict(xx[split])
            metrics[name][split] = dict(accuracy=float(accuracy_score(yy[split], pred)),
                                      macro_f1=float(f1_score(yy[split], pred, labels=LABELS, average="macro", zero_division=0)),
                                      correct=int(np.sum(pred == yy[split])), total=len(pred),
                                      confusion_matrix=confusion_matrix(yy[split], pred, labels=LABELS).tolist())
    evidence = []
    for row, probs in zip(splits["test"], adapted.predict_proba(xx["test"])):
        prediction = str(adapted.classes_[np.argmax(probs)])
        evidence.append(dict(id=row["id"], text=row["text"], expected=row["intent"], predicted=prediction,
                             confidence=float(np.max(probs)), correct=prediction == row["intent"]))
    report = dict(experiment="Small domain adaptation baseline: generic intent pretraining -> SQL intent adaptation",
                  seed=17, split_counts={s: len(rs) for s, rs in splits.items()}, labels=LABELS,
                  vocabulary_size=len(vectorizer.vocabulary_), epochs={"generic_pretrain": 60, "sql_adapt": 45, "sql_scratch": 45},
                  dataset_sha256=hashlib.sha256(data_path.read_bytes()).hexdigest(),
                  versions={"python": platform.python_version(), "numpy": np.__version__, "sklearn": sklearn.__version__},
                  metrics=metrics, held_out_cases=evidence,
                  split_policy="Exact normalized texts are unique. Manually authored stems are split before vocabulary fitting; task concepts overlap. No test/dev fitting or tuning.",
                  limitations=["Synthetic English paraphrases for eight fixed intents and a two-table schema.",
                               "Intent accuracy does not measure executable SQL correctness or real-world robustness.",
                               "No claim of frontier language-model fine-tuning or transfer advantage over scratch."])
    (args.output / "evaluation.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    with (args.output / "held_out_predictions.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(evidence[0]))
        writer.writeheader()
        writer.writerows(evidence)
    (data_dir / "schema.sql").write_text("CREATE TABLE customers (customer_id INTEGER PRIMARY KEY, name TEXT, city TEXT);\nCREATE TABLE orders (order_id INTEGER PRIMARY KEY, customer_id INTEGER, total NUMERIC, FOREIGN KEY(customer_id) REFERENCES customers(customer_id));\n", encoding="utf-8")
    print(json.dumps({"split_counts": report["split_counts"], "vocabulary_size": report["vocabulary_size"], "metrics": metrics}, indent=2))


if __name__ == "__main__":
    main()
