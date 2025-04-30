import path from 'path';
import fs from 'fs';
// eslint-disable-next-line @typescript-eslint/no-require-imports
const binding = require('../build/Release/readstat_binding.node');
const readSas7bdat = binding.readSas7bdat;

// File path to the sample SAS7BDAT file for testing
const sampleFilePath = path.join(__dirname, '/data/sample.sas7bdat');

// Verify the test file exists before running tests
beforeAll(() => {
    if (!fs.existsSync(sampleFilePath)) {
        console.warn(`Test file not found: ${sampleFilePath}`);
        console.warn('Some tests for readSas7bdat may be skipped.');
    }
});

describe('readSas7bdat Native Function', () => {
    // Skip tests if the file doesn't exist
    const runTest = () => {
        if (!fs.existsSync(sampleFilePath)) {
            return test.skip;
        }
        return test;
    };

    runTest()('should read all rows when no optional parameters are provided', () => {
        const result = readSas7bdat(sampleFilePath);
        expect(Array.isArray(result)).toBe(true);
        expect(result.length).toBeGreaterThan(0);

        // Each row should be an array
        expect(Array.isArray(result[0])).toBe(true);
        // Values in the row should have proper types (string, number, or null)
        result[0].forEach((value: string) => {
            expect(['string', 'number', 'object'].includes(typeof value)).toBe(true);
        });
    });

    runTest()('should read with specified start row', () => {
        const startRow = 2;
        const allRows = readSas7bdat(sampleFilePath);
        const partialRows = readSas7bdat(sampleFilePath, startRow);

        expect(Array.isArray(partialRows)).toBe(true);

        // If there are enough rows, partialRows should be shorter than allRows
        if (allRows.length > startRow) {
            expect(partialRows.length).toBeLessThanOrEqual(allRows.length - startRow);
        }
    });

    runTest()('should read specified number of rows', () => {
        const rowCount = 5;
        const rows = readSas7bdat(sampleFilePath, 0, rowCount);

        expect(Array.isArray(rows)).toBe(true);
        expect(rows.length).toBeLessThanOrEqual(rowCount);
    });

    runTest()('should read rows with both start row and row count specified', () => {
        const startRow = 3;
        const rowCount = 4;
        const rows = readSas7bdat(sampleFilePath, startRow, rowCount);

        expect(Array.isArray(rows)).toBe(true);
        expect(rows.length).toBeLessThanOrEqual(rowCount);
    });

    test('should throw error with invalid file path', () => {
        const invalidPath = path.join(__dirname, '/data/nonexistent.sas7bdat');
        expect(() => {
            readSas7bdat(invalidPath);
        }).toThrow();
    });

    test('should throw error with negative start row', () => {
        expect(() => {
            readSas7bdat(sampleFilePath, -5);
        }).toThrow(/Row offset must be non-negative/);
    });

    test('should throw error with invalid row count', () => {
        expect(() => {
            readSas7bdat(sampleFilePath, 0, -5); // -1 is valid but -5 is not
        }).toThrow(/Row limit must be positive or -1/);
    });
});