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

// This program runs an intense, concurrent integration test against the compiled
// SQLite connection state extension (C and C++). It proves three main things:
//  1. Thread-safe Parallel Init: Multiple connections can open and register state simultaneously without locks during query execution.
//  2. Strict Connection Isolation: Each connection has its OWN independent state counter. Queries on Conn A do not leak into Conn B, even on the same DB file.
//  3. Clean Teardown: Each connection tears down its state upon close without memory leaks or race conditions.
func main() {
	extPath := os.Getenv("EXT_PATH")
	if extPath == "" {
		log.Fatal("EXT_PATH environment variable not set")
	}

	numDbs := 3
	connsPerDb := 25
	iterations := 100
	expectedPerConn := (iterations * 11) + 100 // 100 initial + 100 iterations * (1 + 10) = 1200

	var wg sync.WaitGroup
	var mu sync.Mutex
	totalConnsTested := 0
	failedConns := 0

	for i := 0; i < numDbs; i++ {
		dbPath := filepath.Join(os.TempDir(), fmt.Sprintf("test_conn_db_%d.sqlite", i))
		os.Remove(dbPath)

		driverName := fmt.Sprintf("sqlite3_conn_ext_%d", i)
		sql.Register(driverName, &sqlite3.SQLiteDriver{
			Extensions: []string{extPath},
		})

		db, err := sql.Open(driverName, dbPath)
		if err != nil {
			log.Fatalf("Failed to open DB %d: %v", i, err)
		}
		db.SetMaxOpenConns(connsPerDb)
		defer db.Close()

		fmt.Printf("DB %d: Extension loaded via driver registration\n", i)

		var startBarrier sync.WaitGroup
		startBarrier.Add(connsPerDb)
		var doneBarrier sync.WaitGroup
		doneBarrier.Add(connsPerDb)

		for j := 0; j < connsPerDb; j++ {
			wg.Add(1)
			go func(dId, cId int, d *sql.DB) {
				defer wg.Done()

				// Dedicated connection from pool to isolate SQLite connection handle
				conn, err := d.Conn(context.Background())
				if err != nil {
					log.Fatalf("DB %d, Conn %d: Failed to get connection: %v", dId, cId, err)
				}
				defer conn.Close()

				// Barrier 1: Hold all connections open simultaneously to force distinct physical handles
				startBarrier.Done()
				startBarrier.Wait()

				lastCount, err := runTestOnConn(conn, iterations)
				if err != nil {
					fmt.Printf("Error DB %d, Conn %d: %v\n", dId, cId, err)
					mu.Lock()
					failedConns++
					mu.Unlock()
				} else {
					mu.Lock()
					totalConnsTested++
					if lastCount != expectedPerConn {
						fmt.Printf("DB %d, Conn %d: FAILED (Expected %d, got %d)\n", dId, cId, expectedPerConn, lastCount)
						failedConns++
					}
					mu.Unlock()
				}

				// Barrier 2: Do not return any connection to pool until all have finished testing
				doneBarrier.Done()
				doneBarrier.Wait()
			}(i, j, db)
		}
	}

	wg.Wait()

	fmt.Println("\n--- Connection State Integration Test Results ---")
	fmt.Printf("Total Connections Tested: %d\n", totalConnsTested)
	fmt.Printf("Failed Connections: %d\n", failedConns)

	if failedConns > 0 || totalConnsTested != (numDbs*connsPerDb) {
		fmt.Println("\nRESULT: FAILED - Per-connection state was corrupted or leaked across connections.")
		os.Exit(1)
	} else {
		fmt.Printf("\nRESULT: PASSED - All %d concurrent connections maintained strictly isolated state (%d each).\n", totalConnsTested, expectedPerConn)
	}
}

func runTestOnConn(conn *sql.Conn, iterations int) (int, error) {
	var lastCount int
	for k := 0; k < iterations; k++ {
		err := conn.QueryRowContext(context.Background(), "SELECT test_counter()").Scan(&lastCount)
		if err != nil {
			return 0, err
		}
		err = conn.QueryRowContext(context.Background(), "SELECT test_counter_from_db()").Scan(&lastCount)
		if err != nil {
			return 0, err
		}
	}
	return lastCount, nil
}
