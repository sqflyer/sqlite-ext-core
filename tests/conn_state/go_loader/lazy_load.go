package main

import (
	"context"
	"database/sql"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"sync"

	"github.com/mattn/go-sqlite3"
)

// This program verifies that a SQLite connection state extension can be loaded dynamically
// onto already-open database connections without requiring driver-level pre-registration.
func main() {
	extPath := os.Getenv("EXT_PATH")
	if extPath == "" {
		log.Fatal("EXT_PATH environment variable not set")
	}

	numDbs := 3
	connsPerDb := 25
	iterations := 100
	expectedPerConn := (iterations * 11) + 100 // 1200

	for i := 0; i < numDbs; i++ {
		dbPath := filepath.Join(os.TempDir(), fmt.Sprintf("test_conn_lazy_load_%d.sqlite", i))
		os.Remove(dbPath)

		// 1. Open with standard "sqlite3" driver (no extension pre-registered)
		db, err := sql.Open("sqlite3", dbPath)
		if err != nil {
			log.Fatalf("Failed to open standard DB %d: %v", i, err)
		}
		db.SetMaxOpenConns(connsPerDb)

		fmt.Printf("DB %d: Pool opened (Standard Driver)\n", i)

		// 2. Concurrently lazy load extension on each connection
		var wg sync.WaitGroup
		var mu sync.Mutex
		failedConns := 0
		passedConns := 0

		var startBarrier sync.WaitGroup
		startBarrier.Add(connsPerDb)
		var doneBarrier sync.WaitGroup
		doneBarrier.Add(connsPerDb)

		for j := 0; j < connsPerDb; j++ {
			wg.Add(1)
			go func(dId, cId int) {
				defer wg.Done()

				conn, err := db.Conn(context.Background())
				if err != nil {
					log.Fatalf("DB %d, Conn %d: Failed to get connection: %v", dId, cId, err)
				}
				defer conn.Close()

				// Barrier 1: Ensure all connections are open simultaneously
				startBarrier.Done()
				startBarrier.Wait()

				// Dynamically load the extension onto this specific connection
				err = conn.Raw(func(driverConn interface{}) error {
					sqliteConn := driverConn.(*sqlite3.SQLiteConn)
					return sqliteConn.LoadExtension(extPath, "sqlite3_myext_init")
				})
				if err != nil {
					log.Fatalf("DB %d, Conn %d: Lazy Load failed: %v", dId, cId, err)
				}

				var lastCount int
				for k := 0; k < iterations; k++ {
					err := conn.QueryRowContext(context.Background(), "SELECT test_counter()").Scan(&lastCount)
					if err != nil {
						log.Fatalf("DB %d, Conn %d: Query failed: %v", dId, cId, err)
					}
					err = conn.QueryRowContext(context.Background(), "SELECT test_counter_from_db()").Scan(&lastCount)
					if err != nil {
						log.Fatalf("DB %d, Conn %d: Query failed: %v", dId, cId, err)
					}
				}

				mu.Lock()
				if lastCount != expectedPerConn {
					fmt.Printf("DB %d, Conn %d: Unexpected count %d (expected %d)\n", dId, cId, lastCount, expectedPerConn)
					failedConns++
				} else {
					passedConns++
				}
				mu.Unlock()

				// Barrier 2: Keep open until all have finished
				doneBarrier.Done()
				doneBarrier.Wait()
			}(i, j)
		}

		wg.Wait()

		if failedConns > 0 || passedConns != connsPerDb {
			db.Close()
			log.Fatalf("Database %d: Failed connections: %d, Passed: %d", i, failedConns, passedConns)
		}

		fmt.Printf("Database %d: Lazy Load concurrency test (%d connections passed) [OK]\n", i, passedConns)
		db.Close()
	}

	fmt.Println("\nRESULT: PASSED - Dynamic runtime lazy-loading validated across all databases and connections.")
}
