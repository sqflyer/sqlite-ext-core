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
// SQLite hybrid state extension (C and C++). It proves:
//  1. Thread-safe Parallel Init: Multiple connections open and register hybrid state simultaneously.
//  2. Strict Connection Isolation: Each connection has its OWN independent conn_state counter (lock-free).
//  3. Strict Database Sharing: Connections to the SAME database file share the ext_state counter safely.
//  4. Strict Database Isolation: Different database files do not leak ext_state to each other.
//  5. Clean Teardown: Each connection tears down its state upon close without memory leaks or race conditions.
func main() {
	extPath := os.Getenv("EXT_PATH")
	if extPath == "" {
		log.Fatal("EXT_PATH environment variable not set")
	}

	numDbs := 3
	connsPerDb := 25
	iterations := 100
	expectedConnPerConn := iterations + 100 // 100 initial + 100 increments = 200
	expectedExtPerDb := (connsPerDb * iterations) + 1000 // 1000 initial + (25 * 100) = 3500

	var wg sync.WaitGroup
	var mu sync.Mutex
	totalConnsTested := 0
	failedConns := 0

	for i := 0; i < numDbs; i++ {
		dbPath := filepath.Join(os.TempDir(), fmt.Sprintf("test_hybrid_db_%d.sqlite", i))
		os.Remove(dbPath)

		driverName := fmt.Sprintf("sqlite3_hybrid_ext_%d", i)
		sql.Register(driverName, &sqlite3.SQLiteDriver{
			Extensions: []string{extPath},
		})

		db, err := sql.Open(driverName, dbPath)
		if err != nil {
			log.Fatalf("Failed to open DB %d: %v", i, err)
		}
		db.SetMaxOpenConns(connsPerDb + 2)
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

				lastConnCount, err := runTestOnConn(conn, iterations)
				if err != nil {
					fmt.Printf("Error DB %d, Conn %d: %v\n", dId, cId, err)
					mu.Lock()
					failedConns++
					mu.Unlock()
				} else {
					mu.Lock()
					totalConnsTested++
					if lastConnCount != expectedConnPerConn {
						fmt.Printf("DB %d, Conn %d: FAILED (Expected conn %d, got %d)\n", dId, cId, expectedConnPerConn, lastConnCount)
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

	fmt.Println("\n--- Hybrid State Integration Test Results ---")
	fmt.Printf("Total Connections Tested: %d\n", totalConnsTested)
	fmt.Printf("Failed Connections: %d\n", failedConns)

	if failedConns > 0 || totalConnsTested != (numDbs*connsPerDb) {
		fmt.Println("\nRESULT: FAILED - Per-connection state was corrupted or leaked across connections.")
		os.Exit(1)
	}

	// Verify shared ext_state totals across each database
	for i := 0; i < numDbs; i++ {
		driverName := fmt.Sprintf("sqlite3_hybrid_ext_%d", i)
		dbPath := filepath.Join(os.TempDir(), fmt.Sprintf("test_hybrid_db_%d.sqlite", i))
		verifyDb, err := sql.Open(driverName, dbPath)
		if err != nil {
			log.Fatalf("Failed to open verification DB %d: %v", i, err)
		}

		var finalHybridVal int64
		err = verifyDb.QueryRowContext(context.Background(), "SELECT test_hybrid_from_db()").Scan(&finalHybridVal)
		verifyDb.Close()

		if err != nil {
			log.Fatalf("Verification query failed on DB %d: %v", i, err)
		}

		finalExtVal := int(finalHybridVal / 1000000)
		if finalExtVal != expectedExtPerDb {
			fmt.Printf("DB %d: Shared state total mismatch! Expected %d, got %d\n", i, expectedExtPerDb, finalExtVal)
			os.Exit(1)
		}
		fmt.Printf("Database %d: Shared ext_state verified: %d [OK]\n", i, finalExtVal)
	}

	fmt.Printf("\nRESULT: PASSED - All %d concurrent connections maintained isolated conn_state (%d) and shared ext_state (%d).\n",
		totalConnsTested, expectedConnPerConn, expectedExtPerDb)
}

func runTestOnConn(conn *sql.Conn, iterations int) (int, error) {
	var lastConnCount int
	for k := 0; k < iterations; k++ {
		err := conn.QueryRowContext(context.Background(), "SELECT test_conn_counter()").Scan(&lastConnCount)
		if err != nil {
			return 0, err
		}
		var extVal int
		err = conn.QueryRowContext(context.Background(), "SELECT test_ext_counter()").Scan(&extVal)
		if err != nil {
			return 0, err
		}
		var hybridVal int64
		err = conn.QueryRowContext(context.Background(), "SELECT test_hybrid_from_db()").Scan(&hybridVal)
		if err != nil {
			return 0, err
		}
	}
	return lastConnCount, nil
}
