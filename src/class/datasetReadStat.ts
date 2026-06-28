import fs from 'fs';
import {
    ItemDataArray,
    ItemDataObject,
    DatasetMetadata,
    UniqueValues,
    DataType,
    ReadStatMetadata,
    ItemType,
    ItemDescription,
} from './../interfaces/datasetSas7BDat';
import Filter, { BasicFilter } from 'js-array-filter';
import {
    detectReadStatBindingFormat,
    getReadStatBinding,
    ReadStatBindingAdapter,
    ReadStatBindingFormat,
} from './readStatBindings';

type ReadRowsResult = {
    rows: ItemDataArray[];
    lastRow: number;
    endReached: boolean;
};

type ColumnSelection = {
    requestedColumns: string[];
    requestedColumnsInDatasetOrder: string[];
    selectedMetadataColumns: ItemDescription[];
    isProjectedRead: boolean;
};

export type DatasetReadStatOptions = {
    encoding?: BufferEncoding;
    checkExists?: boolean;
    format?: ReadStatBindingFormat;
};

class DatasetReadStat {
    filePath: string;
    stats: fs.Stats | null;
    metadata: DatasetMetadata;
    currentPosition: number;
    allRowsRead: boolean;
    readonly format: ReadStatBindingFormat;
    private metadataLoaded: boolean;
    private encoding: BufferEncoding;
    private readonly binding: ReadStatBindingAdapter;

    constructor(filePath: string, options?: DatasetReadStatOptions) {
        this.filePath = filePath;
        this.currentPosition = 0;
        const {
            encoding = 'utf8',
            checkExists = false,
            format = detectReadStatBindingFormat(filePath),
        } = options || {};
        this.encoding = encoding;
        this.format = format;
        this.binding = getReadStatBinding(format);

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

        const validEncodings: BufferEncoding[] = [
            'ascii',
            'utf8',
            'utf16le',
            'ucs2',
            'base64',
            'latin1',
        ];

        if (!validEncodings.includes(this.encoding)) {
            throw new Error(`Unsupported encoding ${this.encoding}`);
        }

        if (!fs.existsSync(this.filePath)) {
            if (checkExists === true) {
                throw new Error(`Could not read file ${this.filePath}`);
            }
            this.stats = null;
        } else {
            this.stats = fs.statSync(this.filePath);
        }
    }

    private formatTimestamp(timestamp?: number): string {
        if (
            typeof timestamp !== 'number' ||
                    !Number.isFinite(timestamp)
        ) {
            return '';
        }

        return new Date(timestamp * 1000).toISOString();
    };

    async getMetadata(forceReload?: boolean): Promise<DatasetMetadata> {
        if (this.metadataLoaded && !forceReload) {
            return this.metadata;
        }

        try {
            const metadata: ReadStatMetadata = this.binding.getMetadata(
                this.filePath,
            );

            this.metadata = {
                datasetJSONCreationDateTime: this.formatTimestamp(
                    metadata.CreationDateTime,
                ),
                dbLastModifiedDateTime: this.formatTimestamp(
                    metadata.ModifiedDateTime,
                ),
                datasetJSONVersion: '',
                records: metadata.records,
                name: metadata.name || '',
                label: metadata.label || '',
                columns: metadata.columns.map((column) => {
                    const parsedColumn: ItemDescription = {
                        itemOID: column.itemOID,
                        name: column.name,
                        label: column.label || '',
                        length: column.length || 1,
                        dataType: 'string' as ItemType,
                    };
                    if (column.dataType) {
                        parsedColumn.dataType = this.mapReadStatTypeToJsonType(
                            column.dataType,
                        );
                    }
                    if (column.displayFormat) {
                        parsedColumn.displayFormat = column.displayFormat;
                    }
                    return parsedColumn;
                }),
            };

            this.metadataLoaded = true;
            return this.metadata;
        } catch (error) {
            throw new Error(`Failed to read dataset metadata: ${error}`);
        }
    }

    private mapReadStatTypeToJsonType(readStatType: string): ItemType {
        switch (readStatType.toLowerCase()) {
        case 'double':
            return 'double';
        case 'integer':
            return 'integer';
        case 'text':
            return 'string';
        default:
            return 'string';
        }
    }

    private async ensureMetadataLoaded(): Promise<void> {
        if (this.metadataLoaded === false) {
            await this.getMetadata();
        }
    }

    private validateGetDataParams(
        start: number,
        length: number,
        chunkSize: number,
    ): void {
        if (
            (typeof length === 'number' && length <= 0 && length !== -1) ||
            !Number.isInteger(chunkSize) ||
            chunkSize <= 0 ||
            start < 0 ||
            start > this.metadata.records
        ) {
            throw new Error('Invalid start/length parameter values');
        }
    }

    private normalizeFilter(filter?: Filter | BasicFilter): Filter | undefined {
        if (filter === undefined) {
            return undefined;
        }

        if (!Object.prototype.hasOwnProperty.call(filter, 'filterRow')) {
            return new Filter(
                'dataset-json1.1',
                this.metadata.columns,
                filter as BasicFilter,
            );
        }

        return filter as Filter;
    }

    private resolveColumnNames(columns: string[], label: string): string[] {
        const resolvedColumns: string[] = [];
        const missingColumns: string[] = [];

        columns.forEach((columnName) => {
            const matchedColumn = this.metadata.columns.find(
                (column) =>
                    column.name.toLowerCase() === columnName.toLowerCase(),
            );

            if (matchedColumn === undefined) {
                missingColumns.push(columnName);
            } else if (!resolvedColumns.includes(matchedColumn.name)) {
                resolvedColumns.push(matchedColumn.name);
            }
        });

        if (missingColumns.length > 0) {
            throw new Error(`${label} ${missingColumns.join(', ')} not found`);
        }

        return resolvedColumns;
    }

    private getColumnSelection(filterColumns: string[]): ColumnSelection {
        const requestedColumns =
            filterColumns.length > 0
                ? this.resolveColumnNames(filterColumns, 'Columns')
                : this.metadata.columns.map((column) => column.name);
        const requestedColumnsInDatasetOrder = this.metadata.columns
            .filter((column) => requestedColumns.includes(column.name))
            .map((column) => column.name);

        return {
            requestedColumns,
            requestedColumnsInDatasetOrder,
            selectedMetadataColumns: requestedColumns.map(
                (columnName) =>
                    this.metadata.columns.find(
                        (column) => column.name === columnName,
                    )!,
            ),
            isProjectedRead:
                requestedColumnsInDatasetOrder.length <
                this.metadata.columns.length,
        };
    }

    private reorderProjectedRows(
        rows: ItemDataArray[],
        requestedColumns: string[],
        requestedColumnsInDatasetOrder: string[],
    ): ItemDataArray[] {
        if (
            requestedColumnsInDatasetOrder.join(',') ===
            requestedColumns.join(',')
        ) {
            return rows;
        }

        return rows.map((row) =>
            requestedColumns.map((columnName) => {
                const datasetIndex =
                    requestedColumnsInDatasetOrder.indexOf(columnName);
                return row[datasetIndex];
            }),
        );
    }

    private convertRowsToObjects(
        rows: ItemDataArray[],
        selectedMetadataColumns: ItemDescription[],
    ): ItemDataObject[] {
        return rows.map((row) => {
            const obj: ItemDataObject = {};
            const useDatasetIndexes =
                row.length === this.metadata.columns.length;

            selectedMetadataColumns.forEach((column, index) => {
                const rowIndex = useDatasetIndexes
                    ? this.metadata.columns.findIndex(
                        (metadataColumn) =>
                            metadataColumn.name === column.name,
                    )
                    : index;
                obj[column.name] = row[rowIndex];
            });

            return obj;
        });
    }

    private readProjectedRows(
        start: number,
        length: number,
        chunkSize: number,
        selectedColumns: string[],
    ): ReadRowsResult {
        const rows: ItemDataArray[] = [];
        const streamResult = this.binding.readStream(
            this.filePath,
            chunkSize,
            (chunkRows: ItemDataArray[]) => {
                rows.push(...chunkRows);

                if (length !== -1) {
                    return chunkRows.length;
                }

                return undefined;
            },
            start,
            selectedColumns,
        );

        return {
            rows,
            lastRow: streamResult.lastRow,
            endReached:
                streamResult.endReached &&
                streamResult.lastRow >= this.metadata.records - 1,
        };
    }

    private async readUnfilteredRows(
        start: number,
        length: number,
        effectiveChunkSize: number,
        selection: ColumnSelection,
    ): Promise<ReadRowsResult> {
        if (!selection.isProjectedRead) {
            const rows = (await this.binding.readAsync(
                this.filePath,
                start,
                length,
            )) as ItemDataArray[];

            return {
                rows,
                lastRow: start + rows.length - 1,
                endReached: start + rows.length >= this.metadata.records,
            };
        }

        const projectedResult = this.readProjectedRows(
            start,
            length,
            effectiveChunkSize,
            selection.requestedColumnsInDatasetOrder,
        );

        return {
            rows: this.reorderProjectedRows(
                projectedResult.rows,
                selection.requestedColumns,
                selection.requestedColumnsInDatasetOrder,
            ),
            lastRow: projectedResult.lastRow,
            endReached: projectedResult.endReached,
        };
    }

    private readFilteredRows(
        start: number,
        length: number,
        effectiveChunkSize: number,
        filterClass: Filter,
        selection: ColumnSelection,
    ): ReadRowsResult {
        const filterDefinition = filterClass.toBasicFilter();
        const allFilterVariables = filterDefinition.conditions.map((condition) => condition.variable);
        // Some conditions might have compareVariables
        filterDefinition.conditions.forEach((condition) => {
            if (condition.compareVariable) {
                allFilterVariables.push(condition.compareVariable);
            }
        });
        const resolvedFilterColumns = this.resolveColumnNames(
            allFilterVariables,
            'Filter columns',
        );
        const filterProjectionColumns = this.metadata.columns
            .filter((column) => resolvedFilterColumns.includes(column.name))
            .map((column) => column.name);
        const filterProjectionMetadata = filterProjectionColumns.map(
            (columnName) =>
                this.metadata.columns.find(
                    (column) => column.name === columnName,
                )!,
        );
        const projectedFilter = new Filter(
            'dataset-json1.1',
            filterProjectionMetadata,
            filterDefinition,
        );
        const matchedRows: number[] = [];
        const streamResult = this.binding.readStream(
            this.filePath,
            effectiveChunkSize * 100,
            (rows: ItemDataArray[], chunkStartRow: number) => {
                for (let index = 0; index < rows.length; index++) {
                    if (!projectedFilter.filterRow(rows[index])) {
                        continue;
                    }

                    matchedRows.push(chunkStartRow + index);

                    if (length !== -1 && matchedRows.length >= length) {
                        return index + 1;
                    }
                }

                return undefined;
            },
            start,
            filterProjectionColumns,
        );

        const projectedRows =
            matchedRows.length > 0
                ? this.binding.readStream(
                    this.filePath,
                    selection.requestedColumnsInDatasetOrder,
                    matchedRows,
                    matchedRows[0],
                )
                : [];

        return {
            rows: this.reorderProjectedRows(
                projectedRows,
                selection.requestedColumns,
                selection.requestedColumnsInDatasetOrder,
            ),
            lastRow: streamResult.lastRow,
            endReached:
                streamResult.endReached &&
                streamResult.lastRow >= this.metadata.records - 1,
        };
    }

    async getData(props: {
        start?: number;
        length?: number;
        type?: DataType;
        filterColumns?: string[];
        filter?: Filter | BasicFilter;
        chunkSize?: number;
    }): Promise<{
        data: (ItemDataArray | ItemDataObject)[];
        lastRow: number;
        endReached: boolean;
    }> {
        await this.ensureMetadataLoaded();

        let { filterColumns = [] } = props;
        filterColumns = filterColumns.map((item) => item.toLowerCase());

        if (
            this.metadata.columns.length === 0 ||
            this.metadata.records === -1
        ) {
            return Promise.reject(
                new Error('Metadata is not loaded or there are no columns'),
            );
        }

        const {
            start = 0,
            length = -1,
            type = 'array',
            filter,
            chunkSize = 10000,
        } = props;

        try {
            this.validateGetDataParams(start, length, chunkSize);

            const filterClass = this.normalizeFilter(filter);
            const selection = this.getColumnSelection(filterColumns);
            const effectiveChunkSize =
                length === -1 ? chunkSize : Math.min(chunkSize, length);

            const readResult = filterClass
                ? this.readFilteredRows(
                    start,
                    length,
                    effectiveChunkSize,
                    filterClass,
                    selection,
                )
                : await this.readUnfilteredRows(
                    start,
                    length,
                    effectiveChunkSize,
                    selection,
                );

            const data =
                type === 'object'
                    ? this.convertRowsToObjects(
                        readResult.rows,
                        selection.selectedMetadataColumns,
                    )
                    : readResult.rows;

            return {
                data,
                lastRow: readResult.lastRow,
                endReached: readResult.endReached,
            };
        } catch (error) {
            throw new Error(`Failed to read dataset data: ${error}`);
        }
    }

    async *readRecords(props?: {
        start?: number;
        bufferLength?: number;
        type?: DataType;
        filterColumns?: string[];
    }): AsyncGenerator<ItemDataArray | ItemDataObject, void, undefined> {
        await this.ensureMetadataLoaded();

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

            if (!data.data || data.data.length === 0) {
                this.allRowsRead = true;
                break;
            }

            yield* data.data;

            currentPosition = data.lastRow + 1;

            if (currentPosition >= this.metadata.records) {
                this.allRowsRead = true;
                break;
            }
        }
    }

    async getUniqueValues(props: {
        columns: string[];
        limit?: number;
        addCount?: boolean;
        bufferLength?: number;
        sort?: boolean;
    }): Promise<UniqueValues> {
        const {
            limit = 0,
            bufferLength = 1000,
            sort = true,
            addCount = false,
        } = props;
        let { columns } = props;
        const result: UniqueValues = {};

        await this.ensureMetadataLoaded();

        const notFoundColumns: string[] = [];
        columns = columns.map((item) => {
            const column = this.metadata.columns.find(
                (metadataColumn) =>
                    metadataColumn.name.toLowerCase() === item.toLowerCase(),
            );
            if (column === undefined) {
                notFoundColumns.push(item);
                return '';
            }
            return column.name;
        });

        if (notFoundColumns.length > 0) {
            return Promise.reject(
                new Error(`Columns ${notFoundColumns.join(', ')} not found`),
            );
        }

        const uniqueCount: { [name: string]: number } = {};
        columns.forEach((column) => {
            uniqueCount[column] = 0;
            result[column] = { values: [], counts: {} };
        });
        const columnsInDatasetOrder = this.metadata.columns
            .filter((column) => columns.includes(column.name))
            .map((column) => column.name);

        this.binding.readStream(
            this.filePath,
            bufferLength,
            (rows: ItemDataArray[]) => {
                for (let rowIndex = 0; rowIndex < rows.length; rowIndex++) {
                    const row = rows[rowIndex];

                    columnsInDatasetOrder.forEach((column, columnIndex) => {
                        const value = row[columnIndex];

                        if (
                            (limit === 0 || uniqueCount[column] < limit) &&
                            !result[column].values.includes(value)
                        ) {
                            result[column].values.push(value);
                            uniqueCount[column] += 1;
                        }

                        if (addCount) {
                            const valueId =
                                value === null ? 'null' : String(value);
                            result[column].counts[valueId] =
                                result[column].counts[valueId] > 0
                                    ? result[column].counts[valueId] + 1
                                    : 1;
                        }
                    });

                    const isFinished =
                        limit !== 0 &&
                        columns.every((column) => uniqueCount[column] >= limit);

                    if (isFinished) {
                        return rowIndex + 1;
                    }
                }

                return undefined;
            },
            0,
            columnsInDatasetOrder,
        );

        if (sort) {
            Object.keys(result).forEach((key) => {
                result[key].values.sort();
            });
        }

        return result;
    }
}

export default DatasetReadStat;
export { DatasetReadStat };
