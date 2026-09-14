// Run with: node tests/ifw_maintenance_operations_test.js
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname,
    '../packaging/ifw/packages/com.vaporview.maintenancetool/meta/installscript.qs'), 'utf8');

for (const platform of ['windows', 'linux']) {
    for (const oldFramework of [false, true]) {
        const operations = [];
        const context = {
            systemInfo: { productType: platform },
            installer: { versionMatches: () => oldFramework },
            component: {
                addOperation: (...args) => operations.push(args),
                createOperationsForArchive: archive => operations.push(['DefaultExtract', archive])
            }
        };
        vm.createContext(context);
        vm.runInContext(source, context);
        context.Component.prototype.createOperationsForArchive('payload.7z');
        assert.equal(operations[0][0], oldFramework ? 'DefaultExtract' : 'Extract');
        const copies = operations.filter(operation => operation[0] === 'Copy');
        const executions = operations.filter(operation => operation[0] === 'Execute');
        assert.equal(copies.length, platform === 'windows' && !oldFramework ? 2 : 0);
        assert.equal(executions.length, platform === 'windows' ? 2 : 0);
        if (platform === 'linux') {
            assert.equal(JSON.stringify(operations).includes('.exe'), false);
            assert.equal(JSON.stringify(operations).includes('.vaporview-install-root'), false);
        } else {
            assert.deepEqual(executions.map(operation => operation[2]), ['apply', 'verify']);
        }
    }
}
console.log('IFW maintenance operations: Linux/Windows, pre-4.8/4.8+ passed');
