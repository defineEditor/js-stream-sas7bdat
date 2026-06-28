import path from 'path';
import {
    ItemDataArray,
    ReadStatMetadata,
} from './../interfaces/datasetSas7BDat';

export type ReadStatBindingFormat = 'sas7bdat' | 'dta' | 'sav' | 'por';

export type ReadStatStreamBinding = {
    (
        filePath: string,
        chunkSize: number,
        onChunk: (
            rows: ItemDataArray[],
            startRow: number,
        ) => void | boolean | number | { stop?: boolean; rowsUsed?: number },
        startRow?: number,
        selectedColumns?: string[],
    ): { lastRow: number; endReached: boolean };
    (
        filePath: string,
        selectedColumns?: string[],
        selectedRows?: number[],
        startRow?: number,
    ): ItemDataArray[];
};

export type ReadStatBindingAdapter = {
    format: ReadStatBindingFormat;
    read: (
        filePath: string,
        startRow?: number,
        rowCount?: number,
    ) => ItemDataArray[];
    readAsync: (
        filePath: string,
        startRow?: number,
        rowCount?: number,
    ) => Promise<ItemDataArray[]>;
    readStream: ReadStatStreamBinding;
    getMetadata: (filePath: string) => ReadStatMetadata;
};

const projectRoot = path.resolve(__dirname, '..', '..');
// eslint-disable-next-line @typescript-eslint/no-require-imports
const binding = require('node-gyp-build')(projectRoot);

const READSTAT_BINDINGS: Record<ReadStatBindingFormat, ReadStatBindingAdapter> =
    {
        sas7bdat: {
            format: 'sas7bdat',
            read: binding.readSas7bdat,
            readAsync: binding.readSas7bdatAsync,
            readStream: binding.readSas7bdatStream,
            getMetadata: binding.getSAS7BDATMetadata,
        },
        dta: {
            format: 'dta',
            read: binding.readDta,
            readAsync: binding.readDtaAsync,
            readStream: binding.readDtaStream,
            getMetadata: binding.getDtaMetadata,
        },
        sav: {
            format: 'sav',
            read: binding.readSav,
            readAsync: binding.readSavAsync,
            readStream: binding.readSavStream,
            getMetadata: binding.getSavMetadata,
        },
        por: {
            format: 'por',
            read: binding.readPor,
            readAsync: binding.readPorAsync,
            readStream: binding.readPorStream,
            getMetadata: binding.getPorMetadata,
        },
    };

export function getReadStatBinding(
    format: ReadStatBindingFormat,
): ReadStatBindingAdapter {
    const adapter = READSTAT_BINDINGS[format];
    if (adapter === undefined) {
        throw new Error(`Unsupported ReadStat format ${format}`);
    }
    return adapter;
}

export function detectReadStatBindingFormat(
    filePath: string,
): ReadStatBindingFormat {
    const lowerCasePath = filePath.toLowerCase();

    if (lowerCasePath.endsWith('.sas7bdat')) {
        return 'sas7bdat';
    }
    if (lowerCasePath.endsWith('.dta')) {
        return 'dta';
    }
    if (lowerCasePath.endsWith('.sav') || lowerCasePath.endsWith('.zsav')) {
        return 'sav';
    }
    if (lowerCasePath.endsWith('.por')) {
        return 'por';
    }

    throw new Error(`Unsupported file format for ${filePath}`);
}

export function resolveSpssBindingFormat(filePath: string): 'sav' | 'por' {
    const format = detectReadStatBindingFormat(filePath);
    if (format !== 'sav' && format !== 'por') {
        throw new Error(
            `SPSS readers support .sav, .zsav, and .por files. Got ${filePath}`,
        );
    }
    return format;
}
