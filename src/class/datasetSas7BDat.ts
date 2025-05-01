import fs from 'fs';
import path from 'path';
import {
    ItemDataArray,
    ItemDataObject,
    DatasetMetadata,
    UniqueValues,
    DataType,
    Sas7BDatMetadata,
    ItemType,
} from './../interfaces/datasetSas7BDat';
import Filter from 'js-array-filter';

// Import C++ binding with proper error handling
let readSas7bdat: (filePath: string, startRow?: number, rowCount?: number)
    => ItemDataArray[];
let getSAS7BDATMetadata: (filePath: string) => Sas7BDatMetadata;

try {
    const projectRoot = path.resolve(__dirname, '..', '..');
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    const binding = require('node-gyp-build')(projectRoot);
    readSas7bdat = binding.readSas7bdat;
    getSAS7BDATMetadata = binding.getSAS7BDATMetadata;
} catch (err) {
    if (err instanceof Error) {
        throw new Error('Cannot load SAS7BDAT native module. Make sure the module is properly built using node-gyp. ' + err.message);
    }
}

// Main class for SAS7BDAT dataset;
class DatasetSas7BDat {
    // Path to the file;
    filePath: string;
    // Statistics about the file;
    stats: fs.Stats | null;
    // Dataset metadata;
    metadata: DatasetMetadata;
    // Current position in the file;
    currentPosition: number;
    // Flag to indicate if all rows are read;
    allRowsRead: boolean;
    // Metadata loaded
    private metadataLoaded: boolean;
    // Encoding
    private encoding: BufferEncoding;

    /**
     * Initialize SAS7BDAT reader.
     * @constructor
     * @param filePath - Path to the file.
     * @param options - Configuration options
     * @param options.encoding - File encoding. Default is 'utf8'.
     * @param options.checkExists - Throw error if file does not exist. Default is false.
     */
    constructor(
        filePath: string,
        options?: {
            encoding?: BufferEncoding;
            checkExists?: boolean;
        }
    ) {
        this.filePath = filePath;
        this.currentPosition = 0;
        const { encoding = 'utf8', checkExists = false } = options || {};
        this.encoding = encoding;

        this.allRowsRead = false;
        this.metadataLoaded = false;

        this.metadata = {
            datasetJSONCreationDateTime: '',
            datasetJSONVersion: '',
            records: -1,
            name: '',
            label: '',
            columns: [],
        };

        // Get all possible encoding values from BufferEncoding type
        const validEncodings: BufferEncoding[] = [
            'ascii',
            'utf8',
            'utf16le',
            'ucs2',
            'base64',
            'latin1',
        ];

        // Check encoding
        if (!validEncodings.includes(this.encoding)) {
            throw new Error(`Unsupported encoding ${this.encoding}`);
        }

        // Check if file exists;
        if (!fs.existsSync(this.filePath)) {
            if (checkExists === true) {
                throw new Error(`Could not read file ${this.filePath}`);
            } else {
                this.stats = null;
            }
        } else {
            this.stats = fs.statSync(this.filePath);
        }
    }

    /**
     * Get SAS7BDAT metadata
     * @return An object with file metadata.
     */
    async getMetadata(): Promise<DatasetMetadata> {
        if (this.metadataLoaded) {
            return this.metadata;
        }

        try {
            const sasMetadata: Sas7BDatMetadata = getSAS7BDATMetadata(this.filePath);

            // Map ReadStat metadata to our DatasetMetadata interface
            this.metadata = {
                datasetJSONCreationDateTime: new Date(sasMetadata.CreationDateTime || '').toISOString(),
                dbLastModifiedDateTime: new Date(sasMetadata.ModifiedDateTime || '').toISOString(),
                datasetJSONVersion: '',
                records: sasMetadata.records,
                name: sasMetadata.name || '',
                label: sasMetadata.label || '',
                columns: sasMetadata.columns.map(col => {
                    const parsedColumn = {
                        itemOID: col.itemOID,
                        name: col.name,
                        label: col.label || '',
                        length: col.length || 1,
                        dataType: 'string' as ItemType,
                    };
                    if (col.dataType) {
                        parsedColumn.dataType = this.mapSasTypeToJsonType(col.dataType);
                    }
                    return parsedColumn;
                }),
            };

            this.metadataLoaded = true;
            return this.metadata;
        } catch (error) {
            throw new Error(`Failed to read SAS7BDAT metadata: ${error}`);
        }
    }

    /**
     * Maps SAS data types to Dataset-JSON compatible types
     */
    private mapSasTypeToJsonType(sasType: string): ItemType {
        // Map SAS types to Dataset-JSON compatible types
        switch(sasType.toLowerCase()) {
        case 'double':
            return 'double';
        case 'text':
            return 'string';
        default:
            return 'string';
        }
    }

    /**
     * Read observations.
     * @param start - The first row number to read.
     * @param length - The number of records to read.
     * @param type - The type of the returned object.
     * @param filterColumns - The list of columns to return when type is object. If empty, all columns are returned.
     * @param filter - A filter class object used to filter data records when reading the dataset.
     * @return An array of observations.
     */
    async getData(props: {
        start?: number;
        length?: number;
        type?: DataType;
        filterColumns?: string[];
        filter?: Filter;
    }): Promise<(ItemDataArray | ItemDataObject)[]> {
        // Check if metadata is loaded
        if (this.metadataLoaded === false) {
            await this.getMetadata();
        }

        let { filterColumns = [] } = props;

        // Convert filterColumns to lowercase for case-insensitive comparison
        filterColumns = filterColumns.map((item) => item.toLowerCase());

        // Check if metadata is loaded
        if (
            this.metadata.columns.length === 0 ||
            this.metadata.records === -1
        ) {
            return Promise.reject(
                new Error('Metadata is not loaded or there are no columns')
            );
        }

        const { start = 0, length, type = 'array', filter } = props;

        // Check if start and length are valid
        if (
            (typeof length === 'number' && length <= 0) ||
            start < 0 ||
            start > this.metadata.records
        ) {
            return Promise.reject(
                new Error('Invalid start/length parameter values')
            );
        }

        try {
            // Get the column indices for filtering if needed
            const filterColumnIndices = filterColumns.length > 0
                ? filterColumns.map(column =>
                    this.metadata.columns.findIndex(
                        item => item.name.toLowerCase() === column.toLowerCase()
                    )
                )
                : [];

            // Read data from the SAS7BDAT file
            let data: ItemDataArray[] | ItemDataObject[] = readSas7bdat(
                this.filePath, start, length
            ) as ItemDataArray[];

            // If we have a filter, apply it
            if (filter) {
                data = data.filter((row: ItemDataArray) =>
                    filter.filterRow(row)
                );
            }

            // If we're returning arrays and have filtered columns, filter the arrays
            if (type === 'array' && filterColumnIndices.length > 0) {
                return data.map((row: ItemDataArray) =>
                    filterColumnIndices.map(index => row[index])
                );
            } else if (type === 'object') {
                // Convert to object format
                data = data.map((row: ItemDataArray) => {
                    const obj: ItemDataObject = {};
                    this.metadata.columns.forEach((column, index) => {
                        if (filterColumns.length === 0 || filterColumnIndices.includes(index)) {
                            obj[column.name] = row[index];
                        }
                    });
                    return obj;
                });
            }

            return data;
        } catch (error) {
            throw new Error(`Failed to read SAS7BDAT data: ${error}`);
        }
    }

    /**
     * Read observations as an iterable.
     * @param start - The first row number to read.
     * @param bufferLength - The number of records to read in a chunk.
     * @param type - The type of the returned object.
     * @param filterColumns - The list of columns to return when type is object. If empty, all columns are returned.
     * @return An iterable object.
     */
    async *readRecords(props?: {
        start?: number;
        bufferLength?: number;
        type?: DataType;
        filterColumns?: string[];
    }): AsyncGenerator<ItemDataArray | ItemDataObject, void, undefined> {
        // Check if metadata is loaded
        if (this.metadataLoaded === false) {
            await this.getMetadata();
        }

        const {
            start = 0,
            bufferLength = 1000,
            type,
            filterColumns,
        } = props || {};
        let currentPosition = start;

        while (true) {
            const data = await this.getData({
                start: currentPosition,
                length: bufferLength,
                type,
                filterColumns,
            });

            if (!data || data.length === 0) {
                this.allRowsRead = true;
                break;
            }

            yield* data;

            currentPosition += data.length;

            if (currentPosition >= this.metadata.records) {
                this.allRowsRead = true;
                break;
            }
        }
    }

    /**
     * Get unique values observations.
     * @param columns - The list of variables for which to obtain the unique observations.
     * @param limit - The maximum number of values to store. 0 - no limit.
     * @param bufferLength - The number of records to read in a chunk.
     * @param sort - Controls whether to sort the unique values.
     * @return An array of observations.
     */
    async getUniqueValues(props: {
        columns: string[];
        limit?: number;
        addCount?: boolean;
        bufferLength?: number;
        sort?: boolean;
    }): Promise<UniqueValues> {
        const { limit = 0, bufferLength = 1000, sort = true, addCount = false } = props;
        let { columns } = props;
        const result: UniqueValues = {};

        // Check if metadata is loaded
        if (this.metadataLoaded === false) {
            await this.getMetadata();
        }

        const notFoundColumns: string[] = [];
        // Use the case of the columns as specified in the metadata
        columns = columns.map((item) => {
            const column = this.metadata.columns.find(
                (column) => column.name.toLowerCase() === item.toLowerCase()
            );
            if (column === undefined) {
                notFoundColumns.push(item);
                return '';
            } else {
                return column.name;
            }
        });

        if (notFoundColumns.length > 0) {
            return Promise.reject(
                new Error(`Columns ${notFoundColumns.join(', ')} not found`)
            );
        }

        // Store number of unique values found
        const uniqueCount: { [name: string]: number } = {};
        columns.forEach((column) => {
            uniqueCount[column] = 0;
            result[column] = { values: [], counts: {} };
        });

        let isFinished = false;

        for await (const row of this.readRecords({
            bufferLength,
            type: 'object',
            filterColumns: columns,
        }) as AsyncGenerator<ItemDataObject>) {
            columns.forEach((column) => {
                if (
                    (limit === 0 || uniqueCount[column] < limit) &&
                    !result[column].values.includes(row[column])
                ) {
                    result[column].values.push(row[column]);
                    uniqueCount[column] += 1;
                }

                if (addCount) {
                    const valueId = row[column] === null ? 'null' : String(row[column]);
                    result[column].counts[valueId] = result[column].counts[valueId] > 0
                        ? (result[column].counts[valueId] + 1)
                        : 1;
                }
            });

            // Check if all unique values are found
            isFinished = limit !== 0 && Object.keys(uniqueCount).every(
                (key) => uniqueCount[key] >= limit
            );

            if (isFinished) {
                break;
            }
        }

        // Sort result
        if (sort) {
            Object.keys(result).forEach((key) => {
                result[key].values.sort();
            });
        }

        return result;
    }
}

export default DatasetSas7BDat;
