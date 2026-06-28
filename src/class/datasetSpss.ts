import DatasetReadStat, { DatasetReadStatOptions } from './datasetReadStat';
import { resolveSpssBindingFormat } from './readStatBindings';

class DatasetSpss extends DatasetReadStat {
    constructor(
        filePath: string,
        options?: Omit<DatasetReadStatOptions, 'format'>,
    ) {
        super(filePath, {
            ...options,
            format: resolveSpssBindingFormat(filePath),
        });
    }
}

export default DatasetSpss;
export { DatasetSpss };
